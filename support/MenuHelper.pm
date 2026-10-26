package MenuHelper;

use strict;
use warnings;
use Encode qw(encode decode FB_DEFAULT);
use IO::Handle;
use MIME::Base64 qw(encode_base64 decode_base64);

our $VERSION = '12';
my $HELPER_API_ID = '12';
my $HELPER_API_CHECK_PREFIX = "\x1eNPPMENUSCRIPTS_HELPER_API:";

binmode STDIN, ':raw';
binmode STDOUT, ':encoding(UTF-8)';
binmode STDERR, ':encoding(UTF-8)';
$| = 1;

sub new {
    my ($class) = @_;
    return bless { api_checked => 0, stdin_buffer => '' }, $class;
}

sub _utf8_bytes {
    my ($value) = @_;
    return encode('UTF-8', defined($value) ? "$value" : '');
}

sub _base64 {
    return encode_base64(_utf8_bytes($_[0]), '');
}

sub _from_base64 {
    my ($value) = @_;
    return decode('UTF-8', decode_base64($value // ''), FB_DEFAULT);
}

sub _write_line {
    my ($line) = @_;
    STDOUT->flush();
    print STDOUT $line, "\n" or die "NppMenuScripts protocol write failed: $!\n";
    STDOUT->flush() or die "NppMenuScripts protocol flush failed: $!\n";
}

sub _read_line {
    my ($self) = @_;
    while (1) {
        if ($self->{stdin_buffer} =~ s/\A(.*?)\r?\n//s) {
            return $1;
        }
        my $chunk = '';
        my $read = sysread(STDIN, $chunk, 4096);
        die "NppMenuScripts closed the protocol input\n" unless defined($read) && $read > 0;
        $self->{stdin_buffer} .= $chunk;
    }
}

sub _ensure_compatible {
    my ($self) = @_;
    return if $self->{api_checked};
    my $plugin_api_id = $ENV{NPPMENUSCRIPTS_HELPER_API_ID};
    if (!defined($plugin_api_id) || $plugin_api_id ne $HELPER_API_ID) {
        my $loaded_id = defined($plugin_api_id) && length($plugin_api_id)
            ? $plugin_api_id : 'missing';
        die "NppMenuScripts MenuHelper.pm (API ID $HELPER_API_ID) is incompatible with " .
            "the loaded NppMenuScripts.dll (API ID $loaded_id). Copy the matching MenuHelper.pm " .
            "from the NppMenuScripts plugin directory.\n";
    }
    _write_line($HELPER_API_CHECK_PREFIX . $HELPER_API_ID);
    $self->{api_checked} = 1;
}

sub _request {
    my ($self, $prefix, $payload) = @_;
    _ensure_compatible($self);
    _write_line($prefix . ($payload // ''));
    return _read_line($self);
}

sub _default_title {
    my $encoded = $ENV{NPPMENUSCRIPTS_MENU_TITLE_B64};
    return defined($encoded) && length($encoded) ? _from_base64($encoded) : 'NppMenuScripts';
}

sub alert {
    my ($self, $message, $title, $icon) = @_;
    _request($self, "\x1eNPPMENUSCRIPTS_ALERT:", join('|',
        _base64($message),
        _base64(defined($title) ? $title : _default_title()),
        _base64(defined($icon) ? $icon : ''),
    ));
    return;
}

sub prompt {
    my ($self, $message, $default_value, $title, $icon) = @_;
    my $payload = join('|', _base64($message), _base64($default_value),
                  _base64(defined($title) ? $title : _default_title()),
                  _base64(defined($icon) ? $icon : ''));
    my $response = _request($self, "\x1eNPPMENUSCRIPTS_PROMPT:", $payload);
    return undef if $response eq 'CANCEL';
    return _from_base64(substr($response, 3)) if index($response, 'OK:') == 0;
    return $response;
}

sub confirm {
    my ($self, $message, $title, $icon) = @_;
    my $payload = join('|', _base64($message),
                  _base64(defined($title) ? $title : _default_title()),
                  _base64(defined($icon) ? $icon : ''));
    return _request($self, "\x1eNPPMENUSCRIPTS_CONFIRM:", $payload) eq 'OK' ? 1 : 0;
}

sub getFileName {
    my ($self) = @_;
    my $response = _request($self, "\x1eNPPMENUSCRIPTS_GETFILE:");
    return index($response, 'OK:') == 0 ? _from_base64(substr($response, 3)) : '';
}

sub saveFile {
    my ($self) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_SAVEFILE:") eq 'OK' ? 1 : 0;
}

sub getOpenFiles {
    my ($self) = @_;
    my $response = _request($self, "\x1eNPPMENUSCRIPTS_GETOPENFILES:");
    return [] unless index($response, 'OK:') == 0;
    my $payload = substr($response, 3);
    return [] unless length($payload);
    return [ map { _from_base64($_) } split /\|/, $payload ];
}

sub closeFile {
    my ($self, $full_path_file_name) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_CLOSEFILE:", _base64($full_path_file_name)) eq 'OK' ? 1 : 0;
}

sub openFile {
    my ($self, $full_path_file_name) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_OPENFILE:", _base64($full_path_file_name)) eq 'OK' ? 1 : 0;
}

sub openFileInBackground {
    my ($self, $full_path_file_name) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_OPENFILEINBACKGROUND:", _base64($full_path_file_name)) eq 'OK' ? 1 : 0;
}

sub getCursor {
    my ($self) = @_;
    my $response = _request($self, "\x1eNPPMENUSCRIPTS_GETCURSOR:");
    return { line => 0, column => 0 } unless index($response, 'OK:') == 0;
    my ($line, $column) = split /,/, substr($response, 3), 2;
    return { line => 0 + ($line // 0), column => 0 + ($column // 0) };
}

sub setCursor {
    my ($self, $line, $column) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_SETCURSOR:", "$line,$column") eq 'OK' ? 1 : 0;
}

sub getScrollLine {
    my ($self) = @_;
    my $response = _request($self, "\x1eNPPMENUSCRIPTS_GETSCROLLLINE:");
    return index($response, 'OK:') == 0 ? 0 + substr($response, 3) : 0;
}

sub setScrollLine {
    my ($self, $line) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_SETSCROLLLINE:", $line) eq 'OK' ? 1 : 0;
}

sub getLine {
    my ($self) = @_;
    my $response = _request($self, "\x1eNPPMENUSCRIPTS_GETLINE:");
    return index($response, 'OK:') == 0 ? _from_base64(substr($response, 3)) : '';
}

sub setLine {
    my ($self, $value) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_SETLINE:", _base64($value)) eq 'OK' ? 1 : 0;
}

sub getSelection {
    my ($self) = @_;
    my $response = _request($self, "\x1eNPPMENUSCRIPTS_GETSELECTION:");
    return index($response, 'OK:') == 0 ? _from_base64(substr($response, 3)) : '';
}

sub setSelection {
    my ($self, $value) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_SETSELECTION:", _base64($value)) eq 'OK' ? 1 : 0;
}

sub hasSelection {
    my ($self) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_HASSELECTION:") eq 'OK:1' ? 1 : 0;
}

sub getText {
    my ($self) = @_;
    my $response = _request($self, "\x1eNPPMENUSCRIPTS_GETTEXT:");
    return index($response, 'OK:') == 0 ? _from_base64(substr($response, 3)) : '';
}

sub setText {
    my ($self, $value) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_SETTEXT:", _base64($value)) eq 'OK' ? 1 : 0;
}

sub getClipBoard {
    my ($self, $format) = @_;
    my $mode = '';
    if (defined $format) {
        return '' if ref($format);
        $mode = uc($format);
        $mode =~ s/\A\s+|\s+\z//g;
        return '' unless $mode eq '' || $mode eq 'TEXT' || $mode eq 'HTML';
        $mode = 'HTML' if $mode eq 'HTML';
        $mode = '' if $mode eq 'TEXT';
    }
    my $response = _request($self, "\x1eNPPMENUSCRIPTS_GETCLIPBOARD:", $mode);
    return index($response, 'OK:') == 0 ? _from_base64(substr($response, 3)) : '';
}

sub setClipBoard {
    my ($self, $value) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_SETCLIPBOARD:", _base64($value)) eq 'OK' ? 1 : 0;
}

sub runMenu {
    my ($self, $command_id) = @_;
    my $value = defined($command_id) ? "$command_id" : '';
    my $payload;
    if ($value =~ /\A\s*([0-9]+)\s*\z/) {
        $payload = $1;
    } else {
        $payload = 'PATH:' . _base64($value);
    }
    return _request($self, "\x1eNPPMENUSCRIPTS_RUNMENUCOMMAND:", $payload) eq 'OK' ? 1 : 0;
}

sub setStatusBarText {
    my ($self, $value) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_SETSTATUSBARTEXT:", _base64($value)) eq 'OK' ? 1 : 0;
}

sub setOutputPaneTitle {
    my ($self, $value) = @_;
    return _request($self, "\x1eNPPMENUSCRIPTS_SETOUTPUTPANETITLE:", _base64($value)) eq 'OK' ? 1 : 0;
}

1;
