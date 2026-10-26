"""Synchronous Python Helper API for the NppMenuScripts Notepad++ plugin."""

import base64
import os
import sys


_HELPER_API_ID = "12"
_HELPER_API_CHECK_PREFIX = "\x1eNPPMENUSCRIPTS_HELPER_API:"
_api_checked = False


def _encode(value):
    if value is None:
        value = ""
    if not isinstance(value, str):
        value = str(value)
    return base64.b64encode(value.encode("utf-8")).decode("ascii")


def _decode(value):
    return base64.b64decode(value.encode("ascii")).decode("utf-8", errors="replace")


def _write_line(value):
    sys.stdout.buffer.write(value.encode("utf-8") + b"\n")
    sys.stdout.buffer.flush()


def _read_line():
    line = sys.stdin.buffer.readline()
    if not line:
        raise RuntimeError("NppMenuScripts closed the protocol input")
    return line.rstrip(b"\r\n").decode("utf-8", errors="replace")


def _ensure_compatible():
    global _api_checked
    if _api_checked:
        return
    plugin_api_id = os.environ.get("NPPMENUSCRIPTS_HELPER_API_ID")
    if plugin_api_id != _HELPER_API_ID:
        loaded_id = plugin_api_id if plugin_api_id else "missing"
        raise RuntimeError(
            f"NppMenuScripts menu_helper.py (API ID {_HELPER_API_ID}) is incompatible "
            f"with the loaded NppMenuScripts.dll (API ID {loaded_id}). Copy the matching "
            "menu_helper.py from Notepad++\\plugins\\NppMenuScripts."
        )
    _write_line(_HELPER_API_CHECK_PREFIX + _HELPER_API_ID)
    _api_checked = True


def _request(prefix, payload=""):
    _ensure_compatible()
    _write_line(prefix + payload)
    return _read_line()


def _default_title():
    encoded = os.environ.get("NPPMENUSCRIPTS_MENU_TITLE_B64", "")
    if encoded:
        try:
            return _decode(encoded)
        except (ValueError, UnicodeError):
            pass
    return os.environ.get("NPPMENUSCRIPTS_MENU_TITLE", "NppMenuScripts")


def alert(message, title=None, icon=None):
    _request("\x1eNPPMENUSCRIPTS_ALERT:", "|".join((
        _encode(message),
        _encode(title if title is not None else _default_title()),
        _encode(icon if icon is not None else ""),
    )))


def prompt(message, defaultValue="", title=None, icon=None):
    payload = "|".join((
        _encode(message),
        _encode(defaultValue),
        _encode(title if title is not None else _default_title()),
        _encode(icon if icon is not None else ""),
    ))
    response = _request("\x1eNPPMENUSCRIPTS_PROMPT:", payload)
    if response == "CANCEL":
        return None
    if response.startswith("OK:"):
        return _decode(response[3:])
    return response


def confirm(message, title=None, icon=None):
    payload = "|".join((
        _encode(message),
        _encode(title if title is not None else _default_title()),
        _encode(icon if icon is not None else ""),
    ))
    return _request("\x1eNPPMENUSCRIPTS_CONFIRM:", payload) == "OK"


def getFileName():
    response = _request("\x1eNPPMENUSCRIPTS_GETFILE:")
    return _decode(response[3:]) if response.startswith("OK:") else ""


def saveFile():
    return _request("\x1eNPPMENUSCRIPTS_SAVEFILE:") == "OK"


def getOpenFiles():
    response = _request("\x1eNPPMENUSCRIPTS_GETOPENFILES:")
    if not response.startswith("OK:"):
        return []
    payload = response[3:]
    return [_decode(path) for path in payload.split("|")] if payload else []


def closeFile(fullPathFileName):
    return _request("\x1eNPPMENUSCRIPTS_CLOSEFILE:", _encode(fullPathFileName)) == "OK"


def openFile(fullPathFileName):
    return _request("\x1eNPPMENUSCRIPTS_OPENFILE:", _encode(fullPathFileName)) == "OK"


def openFileInBackground(fullPathFileName):
    return _request("\x1eNPPMENUSCRIPTS_OPENFILEINBACKGROUND:", _encode(fullPathFileName)) == "OK"


def getCursor():
    response = _request("\x1eNPPMENUSCRIPTS_GETCURSOR:")
    if not response.startswith("OK:"):
        return {"line": 0, "column": 0}
    parts = response[3:].split(",", 1)
    try:
        return {"line": int(parts[0]), "column": int(parts[1])}
    except (IndexError, ValueError):
        return {"line": 0, "column": 0}


def setCursor(line, column):
    return _request("\x1eNPPMENUSCRIPTS_SETCURSOR:", f"{line},{column}") == "OK"


def getScrollLine():
    response = _request("\x1eNPPMENUSCRIPTS_GETSCROLLLINE:")
    try:
        return int(response[3:]) if response.startswith("OK:") else 0
    except ValueError:
        return 0


def setScrollLine(line):
    return _request("\x1eNPPMENUSCRIPTS_SETSCROLLLINE:", str(line)) == "OK"


def getLine():
    response = _request("\x1eNPPMENUSCRIPTS_GETLINE:")
    return _decode(response[3:]) if response.startswith("OK:") else ""


def setLine(text):
    return _request("\x1eNPPMENUSCRIPTS_SETLINE:", _encode(text)) == "OK"


def getSelection():
    response = _request("\x1eNPPMENUSCRIPTS_GETSELECTION:")
    return _decode(response[3:]) if response.startswith("OK:") else ""


def setSelection(text):
    return _request("\x1eNPPMENUSCRIPTS_SETSELECTION:", _encode(text)) == "OK"


def hasSelection():
    return _request("\x1eNPPMENUSCRIPTS_HASSELECTION:") == "OK:1"


def getText():
    response = _request("\x1eNPPMENUSCRIPTS_GETTEXT:")
    return _decode(response[3:]) if response.startswith("OK:") else ""


def setText(text):
    return _request("\x1eNPPMENUSCRIPTS_SETTEXT:", _encode(text)) == "OK"


def getClipBoard(format=None):
    if format is None:
        mode = ""
    elif isinstance(format, str) and format.strip().upper() in ("", "TEXT"):
        mode = ""
    elif isinstance(format, str) and format.strip().upper() == "HTML":
        mode = "HTML"
    else:
        return ""
    response = _request("\x1eNPPMENUSCRIPTS_GETCLIPBOARD:", mode)
    return _decode(response[3:]) if response.startswith("OK:") else ""


def setClipBoard(text):
    return _request("\x1eNPPMENUSCRIPTS_SETCLIPBOARD:", _encode(text)) == "OK"


def runMenu(commandIDOrPath):
    if isinstance(commandIDOrPath, bool):
        payload = "INVALID"
    elif isinstance(commandIDOrPath, int):
        payload = str(commandIDOrPath)
    elif isinstance(commandIDOrPath, str):
        stripped = commandIDOrPath.strip()
        is_ascii_number = bool(stripped) and all("0" <= char <= "9" for char in stripped)
        payload = stripped if is_ascii_number else "PATH:" + _encode(commandIDOrPath)
    else:
        payload = "INVALID"
    return _request("\x1eNPPMENUSCRIPTS_RUNMENUCOMMAND:", payload) == "OK"


def setStatusBarText(text):
    return _request("\x1eNPPMENUSCRIPTS_SETSTATUSBARTEXT:", _encode(text)) == "OK"


def setOutputPaneTitle(text):
    return _request("\x1eNPPMENUSCRIPTS_SETOUTPUTPANETITLE:", _encode(text)) == "OK"
