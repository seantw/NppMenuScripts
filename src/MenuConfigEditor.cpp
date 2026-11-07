#include "MenuConfigEditor.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shlobj.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "MenuConfigEditorResource.h"
#include "Notepad_plus_msgs.h"
#include "HtmlSecurity.h"

extern "C" {
#include "../third_party/cJSON.h"
}

namespace {

static constexpr INT_PTR CONFIG_EDITOR_WORKSPACE_CHANGED_RESULT = 0x4D4B;
static constexpr UINT TITLE_REFRESH_MESSAGE = WM_APP + 0x4D4;
static constexpr UINT_PTR TITLE_REFRESH_TIMER = 0x4D55;
enum class NodeKind { MenuRoot, ToolbarRoot, TopMenu, Submenu, Script, Html, Action, Execute, Separator };

enum class DropPosition { None, Before, Into, After };
static constexpr UINT_PTR TREE_DRAG_EXPAND_TIMER = 0x4D53;

struct EditorNode {
    NodeKind kind{};
    cJSON* value = nullptr;
    cJSON* parent = nullptr;
    bool topMenu = false;
    unsigned int depth = 0;
    int commandIndex = -1;
    std::wstring environmentText;
    bool environmentTextLoaded = false;
};

struct RichHelpLink {
    HWND control = nullptr;
    LONG start = 0;
    LONG end = 0;
    std::wstring url;
};

struct EditorState {
    MenuConfigEditorContext context;
    HWND hwnd = nullptr;
    HWND tree = nullptr;
    HIMAGELIST treeImages = nullptr;
    HWND tab = nullptr;
    HWND emptyForm = nullptr;
    HWND scriptForm = nullptr;
    HWND htmlForm = nullptr;
    HWND actionForm = nullptr;
    HWND executeForm = nullptr;
    HWND submenuForm = nullptr;
    HWND separatorForm = nullptr;
    HWND settingsForm = nullptr;
    HWND activeItemForm = nullptr;
    HWND controlParent = nullptr;
    HWND header = nullptr;
    HWND titleLabel = nullptr;
    HWND title = nullptr;
    HWND shortcutLabel = nullptr;
    HWND shortcut = nullptr;
    HWND shortcutClear = nullptr;
    HWND shortcutClearHovered = nullptr;
    HWND icon = nullptr;
    HWND scriptLabel = nullptr;
    HWND script = nullptr;
    HWND scriptBrowse = nullptr;
    HWND scriptEdit = nullptr;
    HWND actionLabel = nullptr;
    HWND action = nullptr;
    HWND actionTargetLabel = nullptr;
    HWND actionTarget = nullptr;
    HWND actionHelp = nullptr;
    HWND commandLabel = nullptr;
    HWND command = nullptr;
    HWND commandHelp = nullptr;
    HWND saveBeforeRun = nullptr;
    HWND runInLabel = nullptr;
    HWND runIn = nullptr;
    HWND codePageLabel = nullptr;
    HWND codePage = nullptr;
    HWND codePageHelp = nullptr;
    HWND envLabel = nullptr;
    HWND envList = nullptr;
    HWND envHelp = nullptr;
    HWND envName = nullptr;
    HWND envValue = nullptr;
    HWND envAdd = nullptr;
    HWND envRemove = nullptr;
    HWND info = nullptr;
    HWND scriptHelpText = nullptr;
    HWND htmlHelpText = nullptr;
    HWND htmlAdvancedEnable = nullptr;
    HWND htmlAuthModeGroup = nullptr;
    HWND htmlAuthReconfirm = nullptr;
    HWND htmlAuthTrustFolder = nullptr;
    HWND actionHelpText = nullptr;
    HWND submenuHelpText = nullptr;
    HWND newButton = nullptr;
    HWND runButton = nullptr;
    HWND deleteButton = nullptr;
    HWND upButton = nullptr;
    HWND downButton = nullptr;
    HWND workspacePath = nullptr;
    HWND menuJsonPath = nullptr;
    HWND scriptFolderPath = nullptr;
    HWND iconFolderPath = nullptr;
    HWND workspaceSet = nullptr;
    HWND workspaceOpen = nullptr;
    HWND menuJsonBrowse = nullptr;
    HWND menuJsonOpen = nullptr;
    HWND pathsInfoGroup = nullptr;
    HWND pathsInfoRichEdit = nullptr;
    HWND hideMenuGroup = nullptr;
    HWND hideEncoding = nullptr;
    HWND hideLanguage = nullptr;
    HWND hideTools = nullptr;
    HWND hideMacro = nullptr;
    HWND hideRun = nullptr;
    HWND hideMenuRestart = nullptr;
    HWND scriptFolderSet = nullptr;
    HWND scriptFolderOpen = nullptr;
    HWND iconFolderSet = nullptr;
    HWND iconFolderOpen = nullptr;
    HWND saveButton = nullptr;
    HWND statusBar = nullptr;
    std::wstring statusText;
    std::wstring pendingStatus;
    HWND shortcutTooltip = nullptr;
    HFONT font = nullptr;
    HFONT shortcutClearFont = nullptr;
    HICON customIcon = nullptr;
    HICON iconPreview = nullptr;
    cJSON* root = nullptr;
    cJSON* menu = nullptr;
    cJSON* toolbar = nullptr;
    EditorNode* selected = nullptr;
    std::vector<std::unique_ptr<EditorNode>> nodes;
    std::map<std::wstring, std::wstring> environment;
    std::vector<RichHelpLink> richHelpLinks;
    std::set<std::wstring> persistedExpandedTreePaths;
    bool hasPersistedTreeState = false;
    bool htmlAuthorizationConfirmed = false;
    std::wstring htmlAuthorizedFolder;
    std::wstring htmlAuthorizedSha256;
    std::wstring validationError;
    std::wstring currentIconPath;
    std::wstring shortcutHintText;
    std::wstring workspaceFolder;
    std::wstring iconFolder;
    std::wstring scriptFolder;
    bool toolbarItem = false;
    bool dirty = false;
    bool updating = false;
    bool rebuildingTree = false;
    int activePage = 0;
    bool draggingTreeItem = false;
    HTREEITEM dragItem = nullptr;
    HTREEITEM dragTarget = nullptr;
    HTREEITEM pendingExpand = nullptr;
    DropPosition dropPosition = DropPosition::None;
};

struct HideMenuOption {
    int controlId;
    const char* menuId;
    HWND EditorState::* checkbox;
};

static constexpr HideMenuOption HIDE_MENU_OPTIONS[] = {
    {ID_HIDE_ENCODING, "encoding", &EditorState::hideEncoding},
    {ID_HIDE_LANGUAGE, "language", &EditorState::hideLanguage},
    {ID_HIDE_TOOLS, "tools", &EditorState::hideTools},
    {ID_HIDE_MACRO, "macro", &EditorState::hideMacro},
    {ID_HIDE_RUN, "run", &EditorState::hideRun},
};

static std::wstring tr(EditorState& s, const char* key)
{
    if (s.context.translate) {
        std::wstring value = s.context.translate(key);
        if (!value.empty()) return value;
    }
    return std::wstring(key, key + std::strlen(key));
}

static std::wstring readText(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring value(static_cast<size_t>(std::max(0, length)) + 1, L'\0');
    if (length) GetWindowTextW(hwnd, value.data(), length + 1);
    value.resize(static_cast<size_t>(std::max(0, length)));
    return value;
}

static std::string toUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

static std::wstring fromUtf8(const char* value)
{
    if (!value || !*value) return {};
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (!length) length = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (!length) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), length);
    result.resize(wcslen(result.c_str()));
    return result;
}

static std::wstring nativeLanguageXmlUrl(HWND npp)
{
    std::string filename = "english.xml";
    if (npp) {
        const LRESULT needed = SendMessageW(npp, NPPM_GETNATIVELANGFILENAME, 0, 0);
        if (needed > 0 && needed <= 1024) {
            std::vector<char> buffer(static_cast<size_t>(needed) + 1, '\0');
            const LRESULT copied = SendMessageW(npp, NPPM_GETNATIVELANGFILENAME,
                static_cast<WPARAM>(buffer.size()), reinterpret_cast<LPARAM>(buffer.data()));
            if (copied > 0 && copied <= needed) {
                filename.assign(buffer.data(), static_cast<size_t>(copied));
                const size_t separator = filename.find_last_of("/\\");
                if (separator != std::string::npos) filename.erase(0, separator + 1);
                if (filename.find_last_of('.') == std::string::npos) filename += ".xml";
                if (filename.empty()) filename = "english.xml";
            }
        }
    }

    static constexpr char hex[] = "0123456789ABCDEF";
    std::string escaped;
    for (unsigned char ch : filename) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            escaped.push_back(static_cast<char>(ch));
        } else {
            escaped.push_back('%');
            escaped.push_back(hex[ch >> 4]);
            escaped.push_back(hex[ch & 0x0F]);
        }
    }
    return L"https://github.com/notepad-plus-plus/notepad-plus-plus/blob/master/PowerEditor/installer/nativeLang/" +
        fromUtf8(escaped.c_str());
}

static bool isAbsolute(const std::wstring& path)
{
    return (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
        (path.size() >= 2 && (path[0] == L'\\' || path[0] == L'/') && path[1] == path[0]);
}

static std::wstring fullPath(const std::wstring& path)
{
    if (path.empty()) return {};
    wchar_t buffer[32768]{};
    const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(buffer)), buffer, nullptr);
    std::wstring result = length && length < std::size(buffer) ? std::wstring(buffer, length) : path;
    std::replace(result.begin(), result.end(), L'/', L'\\');
    // Keep a drive root's trailing slash, but remove it from ordinary directories
    // so path-prefix comparisons work whether a picker returned a trailing slash.
    while (result.size() > 3 && result.back() == L'\\') result.pop_back();
    return result;
}

static std::wstring joinPath(const std::wstring& base, const std::wstring& path)
{
    if (isAbsolute(path)) return fullPath(path);
    if (base.empty()) return path;
    return fullPath(base + (base.back() == L'\\' || base.back() == L'/' ? L"" : L"\\") + path);
}

static bool pathWithin(const std::wstring& directory, const std::wstring& path)
{
    if (directory.empty() || path.size() <= directory.size()) return false;
    if (CompareStringOrdinal(directory.c_str(), static_cast<int>(directory.size()), path.c_str(),
                             static_cast<int>(directory.size()), TRUE) != CSTR_EQUAL) return false;
    return directory.back() == L'\\' || path[directory.size()] == L'\\';
}

static std::wstring storedPath(const std::wstring& base, const std::wstring& path)
{
    if (path.empty()) return {};
    // Paths shown in the editor may already be relative to the workspace.
    // Resolve them against that base before converting back to the stored form;
    // GetFullPathName(path) alone would incorrectly use Notepad++'s CWD and can
    // cause a path such as ./script/page/index.html to be prefixed twice.
    const std::wstring absolute = isAbsolute(path) ? fullPath(path) : joinPath(base, path);
    const std::wstring baseFull = fullPath(base);
    if (!baseFull.empty() && CompareStringOrdinal(baseFull.c_str(), static_cast<int>(baseFull.size()),
                                                   absolute.c_str(), static_cast<int>(absolute.size()), TRUE) == CSTR_EQUAL)
        return L".";
    if (pathWithin(baseFull, absolute)) {
        const size_t relativeStart = baseFull.back() == L'\\' ? baseFull.size() : baseFull.size() + 1;
        std::wstring relative = absolute.substr(relativeStart);
        std::replace(relative.begin(), relative.end(), L'\\', L'/');
        return L"./" + relative;
    }
    return absolute;
}

static std::wstring storedScriptPath(const std::wstring& scriptFolder, const std::wstring& path)
{
    if (path.empty()) return {};
    const std::wstring absolute = isAbsolute(path) ? fullPath(path) : fullPath(joinPath(scriptFolder, path));
    const std::wstring baseFull = fullPath(scriptFolder);
    if (pathWithin(baseFull, absolute)) {
        const size_t relativeStart = baseFull.back() == L'\\' ? baseFull.size() : baseFull.size() + 1;
        std::wstring relative = absolute.substr(relativeStart);
        std::replace(relative.begin(), relative.end(), L'\\', L'/');
        return relative;
    }
    return absolute;
}

static std::wstring displayedFolderPath(const std::wstring& base, const std::wstring& path)
{
    const std::wstring absolute = fullPath(path);
    const std::wstring baseFull = fullPath(base);
    if (CompareStringOrdinal(baseFull.c_str(), static_cast<int>(baseFull.size()),
                             absolute.c_str(), static_cast<int>(absolute.size()), TRUE) == CSTR_EQUAL)
        return L".";
    if (pathWithin(baseFull, absolute)) {
        const size_t relativeStart = baseFull.back() == L'\\' ? baseFull.size() : baseFull.size() + 1;
        std::wstring relative = absolute.substr(relativeStart);
        return L".\\" + relative;
    }
    return absolute;
}

static std::wstring resolvedPath(const std::wstring& base, const std::wstring& path)
{
    return isAbsolute(path) ? fullPath(path) : joinPath(base, path);
}

static bool splitIconLocation(const std::wstring& value, std::wstring& path, int& index)
{
    const size_t comma = value.find_last_of(L',');
    if (comma == std::wstring::npos) { path = value; index = 0; return false; }
    wchar_t* end = nullptr;
    const long parsed = wcstol(value.c_str() + comma + 1, &end, 10);
    if (!end || end == value.c_str() + comma + 1 || *end || parsed < INT_MIN || parsed > INT_MAX)
        { path = value; index = 0; return false; }
    path = value.substr(0, comma);
    index = static_cast<int>(parsed);
    return !path.empty();
}

static HICON loadIconLocation(const std::wstring& base, const std::wstring& value, int cx, int cy)
{
    std::wstring path;
    int index = 0;
    const bool indexed = splitIconLocation(value, path, index);
    path = resolvedPath(base, path);
    if (!indexed)
        return static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, cx, cy, LR_LOADFROMFILE));
    HICON large = nullptr, small = nullptr;
    if (!ExtractIconExW(path.c_str(), index, &large, &small, 1)) return nullptr;
    if (large) { if (small) DestroyIcon(small); return large; }
    return small;
}

static bool fileExists(const std::wstring& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool htmlFolderHasIndex(const std::wstring& folder)
{
    const DWORD folderAttributes = GetFileAttributesW(folder.c_str());
    if (folderAttributes == INVALID_FILE_ATTRIBUTES || !(folderAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    return fileExists(joinPath(folder, L"index.html"));
}

static cJSON* member(cJSON* object, const char* key)
{
    return cJSON_GetObjectItemCaseSensitive(object, key);
}

static bool setString(cJSON* object, const char* key, const std::wstring& value)
{
    const std::string utf8 = toUtf8(value);
    cJSON* old = member(object, key);
    if (old && cJSON_IsString(old)) return cJSON_SetValuestring(old, utf8.c_str()) != nullptr;
    cJSON* replacement = cJSON_CreateString(utf8.c_str());
    if (!replacement) return false;
    if (old) return cJSON_ReplaceItemViaPointer(object, old, replacement) != 0;
    return cJSON_AddItemToObject(object, key, replacement) != 0;
}

static bool setBool(cJSON* object, const char* key, bool value)
{
    cJSON* old = member(object, key);
    if (old && cJSON_IsBool(old)) {
        cJSON_SetBoolValue(old, value);
        return true;
    }
    cJSON* replacement = cJSON_CreateBool(value);
    if (!replacement) return false;
    if (old) {
        replacement->string = static_cast<char*>(std::malloc(std::strlen(key) + 1));
        if (!replacement->string) { cJSON_Delete(replacement); return false; }
        std::strcpy(replacement->string, key);
        return cJSON_ReplaceItemViaPointer(object, old, replacement) != 0;
    }
    return cJSON_AddItemToObject(object, key, replacement) != 0;
}

static bool isHideMenuOptionControl(int controlId)
{
    for (const HideMenuOption& option : HIDE_MENU_OPTIONS)
        if (option.controlId == controlId) return true;
    return false;
}

static void loadHideMenuOptions(EditorState& s)
{
    cJSON* configured = member(s.root, "hide_main_menus");
    for (const HideMenuOption& option : HIDE_MENU_OPTIONS) {
        bool checked = false;
        if (cJSON_IsArray(configured)) {
            for (cJSON* item = configured->child; item; item = item->next) {
                if (cJSON_IsString(item) && item->valuestring &&
                    std::strcmp(item->valuestring, option.menuId) == 0) {
                    checked = true;
                    break;
                }
            }
        }
        HWND checkbox = s.*(option.checkbox);
        if (checkbox) SendMessageW(checkbox, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

static bool commitHideMenuOptions(EditorState& s)
{
    cJSON* array = cJSON_CreateArray();
    if (!array) return false;

    bool written[sizeof(HIDE_MENU_OPTIONS) / sizeof(HIDE_MENU_OPTIONS[0])]{};
    cJSON* existing = member(s.root, "hide_main_menus");
    if (cJSON_IsArray(existing)) {
        for (cJSON* item = existing->child; item; item = item->next) {
            if (!cJSON_IsString(item) || !item->valuestring) continue;
            size_t optionIndex = 0;
            for (; optionIndex < sizeof(HIDE_MENU_OPTIONS) / sizeof(HIDE_MENU_OPTIONS[0]); ++optionIndex)
                if (std::strcmp(item->valuestring, HIDE_MENU_OPTIONS[optionIndex].menuId) == 0) break;

            if (optionIndex < sizeof(HIDE_MENU_OPTIONS) / sizeof(HIDE_MENU_OPTIONS[0])) {
                const HideMenuOption& option = HIDE_MENU_OPTIONS[optionIndex];
                HWND checkbox = s.*(option.checkbox);
                const bool checked = checkbox && SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (checked && !written[optionIndex]) {
                    if (!cJSON_AddItemToArray(array, cJSON_CreateString(option.menuId))) {
                        cJSON_Delete(array);
                        return false;
                    }
                    written[optionIndex] = true;
                }
            } else if (!cJSON_AddItemToArray(array, cJSON_CreateString(item->valuestring))) {
                cJSON_Delete(array);
                return false;
            }
        }
    }

    for (size_t i = 0; i < sizeof(HIDE_MENU_OPTIONS) / sizeof(HIDE_MENU_OPTIONS[0]); ++i) {
        const HideMenuOption& option = HIDE_MENU_OPTIONS[i];
        HWND checkbox = s.*(option.checkbox);
        const bool checked = checkbox && SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (checked && !written[i] && !cJSON_AddItemToArray(array, cJSON_CreateString(option.menuId))) {
            cJSON_Delete(array);
            return false;
        }
    }

    cJSON* previous = member(s.root, "hide_main_menus");
    const bool ok = previous
        ? cJSON_ReplaceItemInObjectCaseSensitive(s.root, "hide_main_menus", array) != 0
        : cJSON_AddItemToObject(s.root, "hide_main_menus", array) != 0;
    if (!ok) cJSON_Delete(array);
    return ok;
}

static void setPropertyName(cJSON* item, const std::wstring& name)
{
    const std::string utf8 = toUtf8(name);
    char* replacement = static_cast<char*>(std::malloc(utf8.size() + 1));
    if (!replacement) return;
    std::memcpy(replacement, utf8.c_str(), utf8.size() + 1);
    std::free(item->string);
    item->string = replacement;
}

static HWND control(EditorState& s, const wchar_t* type, const std::wstring& label, DWORD style,
                   int x, int y, int width, int height, int id, DWORD exStyle = 0)
{
    (void)type; (void)style; (void)x; (void)y; (void)width; (void)height; (void)exStyle;
    HWND hwnd = GetDlgItem(s.controlParent ? s.controlParent : s.hwnd, id);
    if (hwnd && !label.empty()) SetWindowTextW(hwnd, label.c_str());
    if (hwnd && s.font) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(s.font), TRUE);
    return hwnd;
}

static COLORREF richHelpBackgroundColor(const EditorState& s, HWND control)
{
    // Rich Edit needs a solid background color. Ask the themed page parent to
    // paint the area behind this child into a temporary bitmap, then sample an
    // interior pixel. This matches the actual ETDT_ENABLETAB page rendering,
    // unlike COLOR_WINDOW or the theme's approximate fill-color hint.
    RECT client{};
    if (control && GetClientRect(control, &client)) {
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        HDC referenceDc = GetDC(control);
        if (referenceDc && width > 0 && height > 0) {
            HDC memoryDc = CreateCompatibleDC(referenceDc);
            HBITMAP bitmap = memoryDc ? CreateCompatibleBitmap(referenceDc, width, height) : nullptr;
            if (memoryDc && bitmap) {
                HGDIOBJ previousBitmap = SelectObject(memoryDc, bitmap);
                if (DrawThemeParentBackground(control, memoryDc, &client) == S_OK) {
                    const COLORREF sampled = GetPixel(memoryDc, width / 2, height / 2);
                    if (sampled != CLR_INVALID) {
                        SelectObject(memoryDc, previousBitmap);
                        DeleteObject(bitmap);
                        DeleteDC(memoryDc);
                        ReleaseDC(control, referenceDc);
                        return sampled;
                    }
                }
                SelectObject(memoryDc, previousBitmap);
            }
            if (bitmap) DeleteObject(bitmap);
            if (memoryDc) DeleteDC(memoryDc);
            ReleaseDC(control, referenceDc);
        } else if (referenceDc) {
            ReleaseDC(control, referenceDc);
        }
    }

    // Fallback for themes that do not support drawing the tab parent.
    if (s.tab) {
        HTHEME theme = OpenThemeData(s.tab, L"TAB");
        if (theme) {
            COLORREF color{};
            // TABP_BODY=10, normal state=1, TMT_FILLCOLORHINT=3821.
            const HRESULT result = GetThemeColor(theme, 10, 1, 3821, &color);
            CloseThemeData(theme);
            if (SUCCEEDED(result)) return color;
        }
    }
    return GetSysColor(COLOR_BTNFACE);
}

static void setRichHelpContent(EditorState& s, HWND control,
                               const std::vector<std::pair<std::wstring, std::wstring>>& paragraphs,
                               int paragraphGapTwips, size_t firstBulletParagraph = 0)
{
    if (!control) return;
    s.richHelpLinks.erase(std::remove_if(s.richHelpLinks.begin(), s.richHelpLinks.end(),
        [control](const RichHelpLink& link) { return link.control == control; }), s.richHelpLinks.end());

    std::wstring content;
    std::vector<std::pair<LONG, LONG>> paragraphRanges;
    for (size_t i = 0; i < paragraphs.size(); ++i) {
        if (i) content.push_back(L'\r');
        const LONG start = static_cast<LONG>(content.size());
        content += paragraphs[i].first;
        const LONG end = static_cast<LONG>(content.size());
        paragraphRanges.emplace_back(start, end);
        if (!paragraphs[i].second.empty()) {
            content += L" (?)";
            s.richHelpLinks.push_back({control, end + 1, end + 4, paragraphs[i].second});
        }
    }

    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(s.font), TRUE);
    SendMessageW(control, EM_SETBKGNDCOLOR, FALSE, richHelpBackgroundColor(s, control));
    SetWindowTextW(control, content.c_str());
    SendMessageW(control, EM_SETEVENTMASK, 0,
                 SendMessageW(control, EM_GETEVENTMASK, 0, 0) | ENM_LINK);

    CHARFORMAT2W characterFormat{};
    characterFormat.cbSize = sizeof(characterFormat);
    characterFormat.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    characterFormat.yHeight = 180;
    characterFormat.crTextColor = GetSysColor(COLOR_WINDOWTEXT);
    wcscpy_s(characterFormat.szFaceName, LF_FACESIZE, L"MS Shell Dlg");
    SendMessageW(control, EM_SETSEL, 0, -1);
    SendMessageW(control, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&characterFormat));

    for (size_t paragraphIndex = 0; paragraphIndex < paragraphRanges.size(); ++paragraphIndex) {
        const auto& range = paragraphRanges[paragraphIndex];
        PARAFORMAT2 paragraphFormat{};
        paragraphFormat.cbSize = sizeof(paragraphFormat);
        paragraphFormat.dwMask = PFM_NUMBERING | PFM_STARTINDENT | PFM_OFFSET |
                                 PFM_SPACEAFTER | PFM_TABSTOPS;
        const bool isBullet = paragraphIndex >= firstBulletParagraph;
        paragraphFormat.wNumbering = isBullet ? PFN_BULLET : 0;
        // Keep bullets at the help block's left edge. The tab stop controls
        // where each bullet paragraph begins; dxOffset aligns wrapped lines.
        paragraphFormat.dxStartIndent = 0;
        paragraphFormat.dxOffset = isBullet ? 240 : 0;
        paragraphFormat.dySpaceAfter = paragraphGapTwips;
        paragraphFormat.cTabCount = isBullet ? 1 : 0;
        if (isBullet) paragraphFormat.rgxTabs[0] = 240;
        SendMessageW(control, EM_SETSEL, range.first, range.second);
        SendMessageW(control, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&paragraphFormat));
    }

    for (const RichHelpLink& link : s.richHelpLinks) {
        if (link.control != control) continue;
        CHARFORMAT2W linkFormat{};
        linkFormat.cbSize = sizeof(linkFormat);
        linkFormat.dwMask = CFM_LINK | CFM_UNDERLINE | CFM_COLOR;
        linkFormat.dwEffects = CFE_LINK | CFE_UNDERLINE;
        linkFormat.crTextColor = GetSysColor(COLOR_HOTLIGHT);
        SendMessageW(control, EM_SETSEL, link.start, link.end);
        SendMessageW(control, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&linkFormat));
    }
    SendMessageW(control, EM_SETSEL, 0, 0);
}

static void setRichPlainText(EditorState& s, HWND control, const std::wstring& text)
{
    if (!control) return;
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(s.font), TRUE);
    SendMessageW(control, EM_SETBKGNDCOLOR, FALSE, richHelpBackgroundColor(s, control));
    SetWindowTextW(control, text.c_str());
    CHARFORMAT2W characterFormat{};
    characterFormat.cbSize = sizeof(characterFormat);
    characterFormat.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    characterFormat.yHeight = 180;
    characterFormat.crTextColor = GetSysColor(COLOR_WINDOWTEXT);
    wcscpy_s(characterFormat.szFaceName, LF_FACESIZE, L"MS Shell Dlg");
    SendMessageW(control, EM_SETSEL, 0, -1);
    SendMessageW(control, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&characterFormat));
    SendMessageW(control, EM_SETSEL, 0, 0);
}

static void show(HWND hwnd, bool visible)
{
    if (hwnd) ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
}

static void showChildDialogAndRepaint(HWND parent, HWND child)
{
    if (!parent || !child) return;
    RECT bounds{};
    const bool hasBounds = GetWindowRect(child, &bounds) != FALSE;
    if (hasBounds) MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&bounds), 2);
    if (hasBounds) InvalidateRect(parent, &bounds, TRUE);
    // Clear the parent first, then paint the page on top. Updating the parent
    // after the child can leave parent pixels over the child dialog.
    if (hasBounds) UpdateWindow(parent);
    ShowWindow(child, SW_SHOW);
    InvalidateRect(child, nullptr, TRUE);
    UpdateWindow(child);
}

static void setEditTextMargins(HWND hwnd, int left = 3, int right = 3)
{
    if (!hwnd) return;
    SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELONG(left, right));
}

static void showShortcutClear(HWND edit, HWND clearButton, bool visible)
{
    if (!clearButton) return;
    if (!visible) {
        ShowWindow(clearButton, SW_HIDE);
        return;
    }

    ShowWindow(clearButton, SW_SHOW);
    // The clear button intentionally overlaps the edit's reserved right edge.
    // Put it at the top of the sibling Z order each time it is shown, then
    // repaint it after the edit so mouse-triggered edit paints cannot cover it.
    SetWindowPos(clearButton, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (edit) InvalidateRect(edit, nullptr, TRUE);
    InvalidateRect(clearButton, nullptr, TRUE);
    UpdateWindow(clearButton);
}

static void setMultilineTextOrigin(HWND hwnd, int left, int top)
{
    if (!hwnd) return;
    RECT rect{};
    SendMessageW(hwnd, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&rect));
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const int borderX = (style & WS_BORDER) ? GetSystemMetrics(SM_CXBORDER) : 0;
    const int borderY = (style & WS_BORDER) ? GetSystemMetrics(SM_CYBORDER) : 0;
    // Microsoft documents that the border must be excluded when reusing an
    // EM_GETRECT result as the EM_SETRECT input.
    rect.left = std::max<LONG>(0, rect.left - borderX + left);
    rect.top = std::max<LONG>(0, rect.top - borderY + top);
    rect.right = std::max<LONG>(rect.left + 1, rect.right - borderX);
    rect.bottom = std::max<LONG>(rect.top + 1, rect.bottom - borderY);
    SendMessageW(hwnd, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&rect));
}

static EditorNode* makeNode(EditorState& s, NodeKind kind, cJSON* value, cJSON* parent,
                            bool topMenu, unsigned int depth)
{
    auto node = std::make_unique<EditorNode>();
    node->kind = kind;
    node->value = value;
    node->parent = parent;
    node->topMenu = topMenu;
    node->depth = depth;
    EditorNode* raw = node.get();
    s.nodes.push_back(std::move(node));
    return raw;
}

static HTREEITEM insertTreeNode(EditorState& s, HTREEITEM parent, const std::wstring& label, EditorNode* node)
{
    TVINSERTSTRUCTW insert{};
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM;
    insert.item.pszText = const_cast<wchar_t*>(label.c_str());
    insert.item.lParam = reinterpret_cast<LPARAM>(node);
    if (node) {
        int image = -1;
        switch (node->kind) {
        case NodeKind::MenuRoot: image = 5; break;
        case NodeKind::ToolbarRoot: image = 6; break;
        case NodeKind::TopMenu:
        case NodeKind::Submenu: image = 0; break;
        case NodeKind::Script: image = 1; break;
        case NodeKind::Html: image = 8; break;
        case NodeKind::Action: image = 2; break;
        case NodeKind::Execute: image = 3; break;
        case NodeKind::Separator: image = 4; break;
        default: break;
        }
        if (image >= 0 && s.treeImages) {
            insert.item.mask |= TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            insert.item.iImage = image;
            insert.item.iSelectedImage = image;
        }
    }
    return reinterpret_cast<HTREEITEM>(SendMessageW(s.tree, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert)));
}

static void initializeTreeImages(EditorState& s)
{
    const int size = std::max(16, GetSystemMetrics(SM_CXSMICON));
    HIMAGELIST images = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 9, 0);
    if (!images) return;
    const int resources[] = {
        IDR_TREE_FOLDER_ICON, IDR_TREE_SCRIPT_ICON,
        IDR_TREE_ACTION_ICON, IDR_TREE_COMMAND_ICON,
        IDR_TREE_SEPARATOR_ICON,
        IDR_TREE_MENU_ICON, IDR_TREE_TOOLBAR_ICON,
        IDR_TREE_FOLDER_OPEN_ICON, IDR_TREE_HTML_ICON
    };
    bool complete = true;
    for (int resource : resources) {
        HICON icon = static_cast<HICON>(LoadImageW(s.context.module, MAKEINTRESOURCEW(resource),
                                                   IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
        if (!icon) {
            complete = false;
            break;
        }
        if (ImageList_AddIcon(images, icon) < 0) complete = false;
        DestroyIcon(icon);
        if (!complete) break;
    }
    if (!complete || ImageList_GetImageCount(images) != static_cast<int>(std::size(resources))) {
        ImageList_Destroy(images);
        return;
    }
    s.treeImages = images;
    TreeView_SetImageList(s.tree, s.treeImages, TVSIL_NORMAL);
}

static void loadSelected(EditorState& s, EditorNode* node);
static bool commitSelected(EditorState& s);

static std::wstring itemTitle(EditorNode* node)
{
    if (!node || !node->value) return {};
    if (node->kind == NodeKind::Separator) return L"────────";
    std::wstring title;
    if (node->topMenu) title = fromUtf8(node->value->string);
    else {
        cJSON* value = member(node->value, "title");
        if (cJSON_IsString(value)) title = fromUtf8(value->valuestring);
    }
    const size_t shortcut = title.find(L'\t');
    if (shortcut != std::wstring::npos) title.resize(shortcut);
    return title;
}

static std::pair<std::wstring, std::wstring> splitShortcut(const std::wstring& title)
{
    const size_t tab = title.find(L'\t');
    return tab == std::wstring::npos ? std::make_pair(title, std::wstring()) :
        std::make_pair(title.substr(0, tab), title.substr(tab + 1));
}

static void refreshSelectedTreeItemTitle(EditorState& s)
{
    EditorNode* node = s.selected;
    if (!node || node->kind == NodeKind::MenuRoot || node->kind == NodeKind::ToolbarRoot ||
        node->kind == NodeKind::Separator)
        return;
    const HTREEITEM itemHandle = TreeView_GetSelection(s.tree);
    if (!itemHandle) return;

    TVITEMW current{};
    current.mask = TVIF_PARAM;
    current.hItem = itemHandle;
    if (!SendMessageW(s.tree, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&current)) ||
        reinterpret_cast<EditorNode*>(current.lParam) != node)
        return;

    const std::wstring label = itemTitle(node);
    std::vector<wchar_t> text(label.begin(), label.end());
    text.push_back(L'\0');
    TVITEMW update{};
    update.mask = TVIF_TEXT;
    update.hItem = itemHandle;
    update.pszText = text.data();
    update.cchTextMax = static_cast<int>(text.size());
    SendMessageW(s.tree, TVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&update));
}

static bool isRunnableNode(NodeKind kind)
{
    return kind == NodeKind::Script || kind == NodeKind::Html ||
           kind == NodeKind::Action || kind == NodeKind::Execute;
}

static void updateRunButton(EditorState& s)
{
    if (!s.runButton) return;
    const bool enabled = !s.dirty && s.selected && isRunnableNode(s.selected->kind) &&
                         s.selected->commandIndex >= 0 && static_cast<bool>(s.context.runSelectedItem);
    EnableWindow(s.runButton, enabled);
}

static void addTreeArray(EditorState& s, HTREEITEM parentItem, cJSON* array,
                         unsigned int depth, int& commandIndex)
{
    for (cJSON* item = array ? array->child : nullptr; item; item = item->next) {
        EditorNode* node = nullptr;
        cJSON* sub = member(item, "sub");
        if (item->string && cJSON_IsArray(item)) {
            node = makeNode(s, NodeKind::TopMenu, item, s.menu, true, depth);
        } else if (cJSON_IsObject(item) && sub && cJSON_IsArray(sub)) {
            node = makeNode(s, NodeKind::Submenu, item, array, false, depth);
        } else if (cJSON_IsObject(item) && !item->child) {
            node = makeNode(s, NodeKind::Separator, item, array, false, depth);
        } else if (member(item, "script")) {
            node = makeNode(s, NodeKind::Script, item, array, false, depth);
        } else if (member(item, "html")) {
            node = makeNode(s, NodeKind::Html, item, array, false, depth);
        } else if (member(item, "action")) {
            node = makeNode(s, NodeKind::Action, item, array, false, depth);
        } else if (member(item, "execute")) {
            node = makeNode(s, NodeKind::Execute, item, array, false, depth);
        } else {
            continue;
        }
        if (isRunnableNode(node->kind)) node->commandIndex = commandIndex++;
        HTREEITEM child = insertTreeNode(s, parentItem, itemTitle(node), node);
        if (node->kind == NodeKind::TopMenu) addTreeArray(s, child, node->value, depth + 1, commandIndex);
        else if (node->kind == NodeKind::Submenu)
            addTreeArray(s, child, member(node->value, "sub"), depth + 1, commandIndex);
    }
}

static void captureTreeExpansion(EditorState& s, HTREEITEM item, std::map<cJSON*, bool>& expansion)
{
    for (HTREEITEM current = item; current; current = TreeView_GetNextSibling(s.tree, current)) {
        TVITEMW data{};
        data.mask = TVIF_PARAM | TVIF_STATE;
        data.stateMask = TVIS_EXPANDED;
        data.hItem = current;
        if (TreeView_GetItem(s.tree, &data)) {
            auto* node = reinterpret_cast<EditorNode*>(data.lParam);
            if (node && node->value)
                expansion[node->value] = (data.state & TVIS_EXPANDED) != 0;
        }
        captureTreeExpansion(s, TreeView_GetChild(s.tree, current), expansion);
    }
}

static std::wstring treeItemPath(EditorState& s, HTREEITEM item, const std::wstring& parentPath,
                                 int siblingIndex, bool rootLevel)
{
    TVITEMW data{};
    data.mask = TVIF_PARAM;
    data.hItem = item;
    EditorNode* node = TreeView_GetItem(s.tree, &data)
        ? reinterpret_cast<EditorNode*>(data.lParam) : nullptr;
    if (rootLevel) {
        if (node && node->kind == NodeKind::MenuRoot) return L"M";
        if (node && node->kind == NodeKind::ToolbarRoot) return L"T";
        return L"?";
    }
    return parentPath + L"/" + std::to_wstring(siblingIndex);
}

static void restoreTreeExpansion(EditorState& s, HTREEITEM item,
                                 const std::map<cJSON*, bool>& expansion, bool initialBuild,
                                 const std::wstring& parentPath = L"")
{
    int siblingIndex = 0;
    for (HTREEITEM current = item; current; current = TreeView_GetNextSibling(s.tree, current)) {
        TVITEMW data{};
        data.mask = TVIF_PARAM;
        data.hItem = current;
        EditorNode* node = TreeView_GetItem(s.tree, &data)
            ? reinterpret_cast<EditorNode*>(data.lParam) : nullptr;
        const std::wstring path = treeItemPath(s, current, parentPath, siblingIndex++, parentPath.empty());
        bool expanded = false;
        bool hasSavedState = false;
        if (node && node->value) {
            const auto saved = expansion.find(node->value);
            if (saved != expansion.end()) {
                expanded = saved->second;
                hasSavedState = true;
            }
        }
        if (!hasSavedState && initialBuild && s.hasPersistedTreeState) {
            expanded = s.persistedExpandedTreePaths.find(path) != s.persistedExpandedTreePaths.end();
            hasSavedState = true;
        }
        if (hasSavedState)
            TreeView_Expand(s.tree, current, expanded ? TVE_EXPAND : TVE_COLLAPSE);
        else if (initialBuild && node &&
                 (node->kind == NodeKind::MenuRoot || node->kind == NodeKind::ToolbarRoot))
            TreeView_Expand(s.tree, current, TVE_EXPAND);
        restoreTreeExpansion(s, TreeView_GetChild(s.tree, current), expansion, initialBuild, path);
    }
}

static void collectExpandedTreePaths(EditorState& s, HTREEITEM item, const std::wstring& parentPath,
                                     bool rootLevel, std::set<std::wstring>& expandedPaths)
{
    int siblingIndex = 0;
    for (HTREEITEM current = item; current; current = TreeView_GetNextSibling(s.tree, current)) {
        const std::wstring path = treeItemPath(s, current, parentPath, siblingIndex++, rootLevel);
        TVITEMW data{};
        data.mask = TVIF_STATE;
        data.stateMask = TVIS_EXPANDED;
        data.hItem = current;
        if (TreeView_GetItem(s.tree, &data) && (data.state & TVIS_EXPANDED) &&
            TreeView_GetChild(s.tree, current))
            expandedPaths.insert(path);
        collectExpandedTreePaths(s, TreeView_GetChild(s.tree, current), path, false, expandedPaths);
    }
}

static void savePersistentTreeExpansion(EditorState& s)
{
    if (s.context.settingsFilePath.empty() || !s.tree || !IsWindow(s.tree)) return;
    std::set<std::wstring> expandedPaths;
    collectExpandedTreePaths(s, TreeView_GetChild(s.tree, TVI_ROOT), L"", true, expandedPaths);
    std::wstring serialized;
    for (const auto& path : expandedPaths) {
        if (!serialized.empty()) serialized.push_back(L'|');
        serialized += path;
    }
    const size_t slash = s.context.settingsFilePath.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        const std::wstring directory = s.context.settingsFilePath.substr(0, slash);
        SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
    }
    WritePrivateProfileStringW(L"TreeState", L"expanded", serialized.c_str(),
                               s.context.settingsFilePath.c_str());
}

static void loadPersistentTreeExpansion(EditorState& s)
{
    if (s.context.settingsFilePath.empty()) return;
    constexpr wchar_t missing[] = L"__NMS_TREE_STATE_MISSING__";
    wchar_t value[32768] = {};
    GetPrivateProfileStringW(L"TreeState", L"expanded", missing, value,
                             static_cast<DWORD>(std::size(value)), s.context.settingsFilePath.c_str());
    if (std::wstring(value) == missing) return;
    s.hasPersistedTreeState = true;
    const std::wstring serialized(value);
    size_t start = 0;
    while (start < serialized.size()) {
        const size_t end = serialized.find(L'|', start);
        const std::wstring path = serialized.substr(start, end == std::wstring::npos ? end : end - start);
        if (!path.empty()) s.persistedExpandedTreePaths.insert(path);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
}

static void rebuildTree(EditorState& s, cJSON* selectedValue = nullptr)
{
    // Deleting and repopulating tree items synchronously emits selection notifications.
    // Ignore those notifications while node pointers are being replaced.
    std::map<cJSON*, bool> expansion;
    const bool initialBuild = TreeView_GetCount(s.tree) == 0;
    if (!initialBuild)
        captureTreeExpansion(s, TreeView_GetChild(s.tree, TVI_ROOT), expansion);
    s.rebuildingTree = true;
    s.selected = nullptr;
    SendMessageW(s.tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(s.tree);
    s.nodes.clear();
    EditorNode* menuRoot = makeNode(s, NodeKind::MenuRoot, s.menu, s.root, false, 0);
    EditorNode* toolbarRoot = makeNode(s, NodeKind::ToolbarRoot, s.toolbar, s.root, false, 0);
    HTREEITEM menuItem = insertTreeNode(s, TVI_ROOT, tr(s, "ConfigMenuRoot"), menuRoot);
    int commandIndex = 0;
    addTreeArray(s, menuItem, s.menu, 1, commandIndex);
    HTREEITEM toolbarItem = insertTreeNode(s, TVI_ROOT, tr(s, "ConfigToolbarRoot"), toolbarRoot);
    addTreeArray(s, toolbarItem, s.toolbar, 1, commandIndex);
    restoreTreeExpansion(s, TreeView_GetChild(s.tree, TVI_ROOT), expansion, initialBuild);
    HTREEITEM selectedItem = nullptr;
    const int count = TreeView_GetCount(s.tree);
    (void)count;
    std::function<void(HTREEITEM)> find = [&](HTREEITEM item) {
        for (HTREEITEM current = item; current && !selectedItem; current = TreeView_GetNextSibling(s.tree, current)) {
            TVITEMW data{}; data.mask = TVIF_PARAM; data.hItem = current;
            TreeView_GetItem(s.tree, &data);
            auto* node = reinterpret_cast<EditorNode*>(data.lParam);
            if (selectedValue && node && node->value == selectedValue) { selectedItem = current; break; }
            find(TreeView_GetChild(s.tree, current));
        }
    };
    find(TreeView_GetChild(s.tree, TVI_ROOT));
    if (!selectedItem) selectedItem = menuItem;
    TreeView_SelectItem(s.tree, selectedItem);
    if (selectedValue)
        TreeView_EnsureVisible(s.tree, selectedItem);
    TVITEMW selectedData{};
    selectedData.mask = TVIF_PARAM;
    selectedData.hItem = selectedItem;
    if (TreeView_GetItem(s.tree, &selectedData))
        loadSelected(s, reinterpret_cast<EditorNode*>(selectedData.lParam));
    s.rebuildingTree = false;
    SendMessageW(s.tree, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(s.tree, nullptr, nullptr,
                 RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

static EditorNode* treeNode(EditorState& s, HTREEITEM item)
{
    if (!item) return nullptr;
    TVITEMW data{};
    data.mask = TVIF_PARAM;
    data.hItem = item;
    return TreeView_GetItem(s.tree, &data)
        ? reinterpret_cast<EditorNode*>(data.lParam) : nullptr;
}

static bool treeItemContains(EditorState& s, HTREEITEM parent, HTREEITEM item)
{
    for (HTREEITEM current = item; current; current = TreeView_GetParent(s.tree, current))
        if (current == parent) return true;
    return false;
}

static unsigned int deepestTreeDepth(EditorState& s, HTREEITEM item)
{
    EditorNode* node = treeNode(s, item);
    unsigned int deepest = node ? node->depth : 0;
    for (HTREEITEM child = TreeView_GetChild(s.tree, item); child;
         child = TreeView_GetNextSibling(s.tree, child))
        deepest = std::max(deepest, deepestTreeDepth(s, child));
    return deepest;
}

static HCURSOR systemCursor(WORD resourceId)
{
    return LoadCursorW(nullptr, MAKEINTRESOURCEW(resourceId));
}

static bool dropDestination(EditorState& s, HTREEITEM sourceItem, HTREEITEM targetItem,
                            DropPosition position, cJSON*& destination)
{
    destination = nullptr;
    EditorNode* source = treeNode(s, sourceItem);
    EditorNode* target = treeNode(s, targetItem);
    if (!source || !target || !source->parent || sourceItem == targetItem ||
        source->kind == NodeKind::MenuRoot || source->kind == NodeKind::ToolbarRoot ||
        target->kind == NodeKind::MenuRoot || target->kind == NodeKind::ToolbarRoot)
        return false;

    const bool sourceToolbar = source->parent == s.toolbar;
    const bool targetToolbar = target->parent == s.toolbar;
    if (sourceToolbar != targetToolbar) return false;

    if (position == DropPosition::Before || position == DropPosition::After) {
        if (treeItemContains(s, sourceItem, targetItem)) return false;
        if (source->topMenu) {
            // Top-level menu entries live in an object, so keep them among their siblings.
            if (source->parent != target->parent) return false;
            destination = source->parent;
            return true;
        }
        destination = target->parent;
        if (!cJSON_IsArray(destination)) {
            destination = nullptr;
            return false;
        }
        const unsigned int sourceSubtreeDepth = deepestTreeDepth(s, sourceItem) - source->depth;
        if (target->depth + sourceSubtreeDepth > 17) {
            destination = nullptr;
            return false;
        }
        return true;
    }

    if (position != DropPosition::Into || sourceToolbar || source->topMenu ||
        treeItemContains(s, sourceItem, targetItem)) return false;

    if (target->kind == NodeKind::TopMenu) destination = target->value;
    else if (target->kind == NodeKind::Submenu) destination = member(target->value, "sub");
    if (!cJSON_IsArray(destination)) {
        destination = nullptr;
        return false;
    }

    const unsigned int sourceSubtreeDepth = deepestTreeDepth(s, sourceItem) - source->depth;
    if (target->depth + 1 + sourceSubtreeDepth > 17) {
        destination = nullptr;
        return false;
    }
    return true;
}

static void clearTreeDropFeedback(EditorState& s)
{
    if (s.hwnd) KillTimer(s.hwnd, TREE_DRAG_EXPAND_TIMER);
    s.pendingExpand = nullptr;
    s.dragTarget = nullptr;
    s.dropPosition = DropPosition::None;
    TreeView_SetInsertMark(s.tree, nullptr, FALSE);
    TreeView_SelectDropTarget(s.tree, nullptr);
}

static bool updateTreeDrag(EditorState& s)
{
    POINT screen{};
    if (!GetCursorPos(&screen)) return false;
    POINT client = screen;
    ScreenToClient(s.tree, &client);
    RECT clientRect{};
    GetClientRect(s.tree, &clientRect);
    if (!PtInRect(&clientRect, client)) {
        clearTreeDropFeedback(s);
        SetCursor(systemCursor(32648)); // IDC_NO
        return false;
    }

    TVHITTESTINFO hit{};
    hit.pt = client;
    HTREEITEM targetItem = TreeView_HitTest(s.tree, &hit);
    RECT row{};
    if (!targetItem || !TreeView_GetItemRect(s.tree, targetItem, &row, TRUE)) {
        clearTreeDropFeedback(s);
        SetCursor(systemCursor(32648)); // IDC_NO
        return false;
    }

    EditorNode* target = treeNode(s, targetItem);
    const int height = std::max(1, static_cast<int>(row.bottom - row.top));
    const int relativeY = client.y - row.top;
    DropPosition position = DropPosition::None;
    EditorNode* source = treeNode(s, s.dragItem);
    const bool container = source && !source->topMenu && target &&
        (target->kind == NodeKind::TopMenu || target->kind == NodeKind::Submenu);
    if (container && relativeY >= height / 4 && relativeY < (height * 3) / 4)
        position = DropPosition::Into;
    else
        position = relativeY < height / 2 ? DropPosition::Before : DropPosition::After;

    cJSON* destination = nullptr;
    if (!dropDestination(s, s.dragItem, targetItem, position, destination)) {
        clearTreeDropFeedback(s);
        SetCursor(systemCursor(32648)); // IDC_NO
        return false;
    }

    if (s.dragTarget != targetItem || s.dropPosition != position) {
        clearTreeDropFeedback(s);
        s.dragTarget = targetItem;
        s.dropPosition = position;
        if (position == DropPosition::Into) {
            TreeView_SelectDropTarget(s.tree, targetItem);
            TVITEMW state{};
            state.mask = TVIF_STATE;
            state.stateMask = TVIS_EXPANDED;
            state.hItem = targetItem;
            if (TreeView_GetItem(s.tree, &state) && !(state.state & TVIS_EXPANDED)) {
                s.pendingExpand = targetItem;
                SetTimer(s.hwnd, TREE_DRAG_EXPAND_TIMER, 650, nullptr);
            }
        } else {
            TreeView_SetInsertMark(s.tree, targetItem, position == DropPosition::After);
        }
    }
    SetCursor(systemCursor(32646)); // IDC_SIZEALL
    return true;
}

static bool reorderLinked(cJSON* parent, cJSON* item, bool up);

static int linkedIndex(cJSON* parent, cJSON* item)
{
    int index = 0;
    for (cJSON* current = parent ? parent->child : nullptr; current; current = current->next, ++index)
        if (current == item) return index;
    return -1;
}

static bool performTreeDrop(EditorState& s, HTREEITEM sourceItem, HTREEITEM targetItem,
                           DropPosition position)
{
    cJSON* destination = nullptr;
    if (!dropDestination(s, sourceItem, targetItem, position, destination)) return false;
    EditorNode* source = treeNode(s, sourceItem);
    EditorNode* target = treeNode(s, targetItem);
    if (!source || !target) return false;

    cJSON* value = source->value;
    if (position == DropPosition::Before || position == DropPosition::After) {
        const bool after = position == DropPosition::After;
        if (source->topMenu) {
            int sourceIndex = linkedIndex(source->parent, value);
            const int targetIndex = linkedIndex(source->parent, target->value);
            if (sourceIndex < 0 || targetIndex < 0) return false;
            const int desired = targetIndex + (after ? 1 : 0) - (sourceIndex < targetIndex + (after ? 1 : 0) ? 1 : 0);
            while (sourceIndex > desired) {
                if (!reorderLinked(source->parent, value, true)) return false;
                --sourceIndex;
            }
            while (sourceIndex < desired) {
                if (!reorderLinked(source->parent, value, false)) return false;
                ++sourceIndex;
            }
        } else {
            cJSON* oldParent = source->parent;
            cJSON* destinationParent = target->parent;
            int oldIndex = -1;
            for (int i = 0; i < cJSON_GetArraySize(oldParent); ++i)
                if (cJSON_GetArrayItem(oldParent, i) == value) { oldIndex = i; break; }
            if (oldIndex < 0) return false;
            cJSON* detached = cJSON_DetachItemViaPointer(oldParent, value);
            if (!detached) return false;
            const int targetIndex = linkedIndex(destinationParent, target->value);
            const int insertIndex = targetIndex + (after ? 1 : 0);
            if (targetIndex < 0 || !cJSON_InsertItemInArray(destinationParent, insertIndex, detached)) {
                cJSON_InsertItemInArray(oldParent, oldIndex, detached);
                return false;
            }
        }
    } else {
        cJSON* oldParent = source->parent;
        int oldIndex = -1;
        for (int i = 0; i < cJSON_GetArraySize(oldParent); ++i)
            if (cJSON_GetArrayItem(oldParent, i) == value) { oldIndex = i; break; }
        cJSON* detached = cJSON_DetachItemViaPointer(oldParent, value);
        if (!detached) return false;
        if (!cJSON_AddItemToArray(destination, detached)) {
            if (oldIndex >= 0) cJSON_InsertItemInArray(oldParent, oldIndex, detached);
            return false;
        }
    }

    s.dirty = true;
    rebuildTree(s, value);
    return true;
}

static void endTreeDrag(EditorState& s, bool applyDrop)
{
    const HTREEITEM source = s.dragItem;
    const HTREEITEM target = s.dragTarget;
    const DropPosition position = s.dropPosition;
    s.draggingTreeItem = false;
    clearTreeDropFeedback(s);
    s.dragItem = nullptr;
    if (GetCapture() == s.hwnd) ReleaseCapture();
    SetCursor(systemCursor(32512)); // IDC_ARROW
    if (applyDrop && source && target && position != DropPosition::None)
        performTreeDrop(s, source, target, position);
    updateRunButton(s);
}

static void beginTreeDrag(EditorState& s, HTREEITEM item)
{
    EditorNode* node = treeNode(s, item);
    if (!node || !node->parent || node->kind == NodeKind::MenuRoot || node->kind == NodeKind::ToolbarRoot)
        return;
    if (!commitSelected(s)) {
        MessageBoxW(s.hwnd, tr(s, "ConfigDuplicateMenuTitle").c_str(), tr(s, "ConfigTitle").c_str(),
                    MB_OK | MB_ICONWARNING);
        return;
    }
    s.dragItem = item;
    s.dragTarget = nullptr;
    s.dropPosition = DropPosition::None;
    s.draggingTreeItem = true;
    SetCapture(s.hwnd);
    updateTreeDrag(s);
}

static void loadEnvironment(EditorState& s, cJSON* execute)
{
    s.environment.clear();
    cJSON* env = member(execute, "env");
    for (cJSON* item = env ? env->child : nullptr; item; item = item->next)
        if (item->string && cJSON_IsString(item)) s.environment[fromUtf8(item->string)] = fromUtf8(item->valuestring);
}

static void loadEnvironmentText(EditorState& s, EditorNode* node)
{
    if (!node || node->kind != NodeKind::Execute) return;
    if (!node->environmentTextLoaded) {
        std::wstring text;
        cJSON* execute = member(node->value, "execute");
        cJSON* env = member(execute, "env");
        for (cJSON* item = env ? env->child : nullptr; item; item = item->next) {
            if (!item->string || !cJSON_IsString(item)) continue;
            if (!text.empty()) text += L"\r\n";
            text += fromUtf8(item->string) + L"=" + fromUtf8(item->valuestring);
        }
        node->environmentText = std::move(text);
        node->environmentTextLoaded = true;
    }
    SetWindowTextW(s.envList, node->environmentText.c_str());
    SendMessageW(s.envList, EM_SETSEL, 0, 0);
    SendMessageW(s.envList, EM_SCROLLCARET, 0, 0);
}

static std::map<std::wstring, std::wstring> parseEnvironmentText(const std::wstring& text)
{
    std::map<std::wstring, std::wstring> values;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find_first_of(L"\r\n", start);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring line = text.substr(start, end - start);
        if (!line.empty() && line[0] != L'#') {
            const size_t equals = line.find(L'=');
            if (equals != std::wstring::npos && equals > 0) values[line.substr(0, equals)] = line.substr(equals + 1);
        }
        if (end == text.size()) break;
        start = end + 1;
        if (text[end] == L'\r' && start < text.size() && text[start] == L'\n') ++start;
    }
    return values;
}

static void refreshEnvironment(EditorState& s)
{
    SendMessageW(s.envList, LB_RESETCONTENT, 0, 0);
    for (const auto& pair : s.environment) {
        std::wstring row = pair.first + L" = " + pair.second;
        SendMessageW(s.envList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    }
}

static void fillRunIn(EditorState& s, const std::wstring& selected)
{
    SendMessageW(s.runIn, CB_RESETCONTENT, 0, 0);
    const struct { const wchar_t* value; const char* label; } values[] = {
        {L"output_pane", "OutputPane"}, {L"console", "ConfigConsole"}, {L"windows", "ConfigWindows"}
    };
    for (const auto& value : values) {
        const std::wstring label = tr(s, value.label);
        const int index = static_cast<int>(SendMessageW(s.runIn, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(s.runIn, CB_SETITEMDATA, index, reinterpret_cast<LPARAM>(value.value));
        if (selected == value.value) SendMessageW(s.runIn, CB_SETCURSEL, index, 0);
    }
    if (SendMessageW(s.runIn, CB_GETCURSEL, 0, 0) == CB_ERR) SendMessageW(s.runIn, CB_SETCURSEL, 0, 0);
}

static std::wstring selectedRunIn(EditorState& s)
{
    const LRESULT index = SendMessageW(s.runIn, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) return L"output_pane";
    const wchar_t* value = reinterpret_cast<const wchar_t*>(SendMessageW(s.runIn, CB_GETITEMDATA, index, 0));
    return value ? value : L"output_pane";
}

static void fillActionCombo(EditorState& s, const std::wstring& token)
{
    SendMessageW(s.action, CB_RESETCONTENT, 0, 0);
    const struct { const char* key; int value; const wchar_t* token; } values[] = {
        {"ConfigRunCurrentFile", 1, L"RUN_CURRENT_FILE"},
        {"ConfigStopScript", 2, L"STOP_SCRIPT"},
        {"ConfigRunMenuCommand", 3, L"MENU:"},
        {"ConfigOpenMenuConfig", 4, L"CONFIGURE_MENU"}
    };
    int selected = 0;
    for (size_t i = 0; i < std::size(values); ++i) {
        const std::wstring label = tr(s, values[i].key);
        const int index = static_cast<int>(SendMessageW(s.action, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
        SendMessageW(s.action, CB_SETITEMDATA, index, values[i].value);
        if ((values[i].value == 1 && token == L"RUN_CURRENT_FILE") ||
            (values[i].value == 2 && token == L"STOP_SCRIPT") ||
            (values[i].value == 3 && token.rfind(L"MENU:", 0) == 0) ||
            (values[i].value == 4 && token == L"CONFIGURE_MENU")) selected = index;
    }
    SendMessageW(s.action, CB_SETCURSEL, selected, 0);
}

static void updateIconPreview(EditorState& s)
{
    if (s.customIcon) { DestroyIcon(s.customIcon); s.customIcon = nullptr; }
    HICON icon = nullptr;
    if (!s.currentIconPath.empty()) {
        icon = loadIconLocation(s.iconFolder, s.currentIconPath, 32, 32);
        if (icon) s.customIcon = icon;
    }
    if (!icon) icon = static_cast<HICON>(LoadImageW(s.context.module, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_SHARED));
    s.iconPreview = icon;
    InvalidateRect(s.icon, nullptr, TRUE);
    if (!s.currentIconPath.empty() && !s.customIcon) SetWindowTextW(s.icon, tr(s, "ConfigChangeIcon").c_str());
}

static LRESULT CALLBACK shortcutSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK helpLinkSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

static HWND itemFormFor(EditorState& s, EditorNode* node)
{
    if (!node || node->kind == NodeKind::MenuRoot || node->kind == NodeKind::ToolbarRoot)
        return s.emptyForm;
    switch (node->kind) {
    case NodeKind::TopMenu:
    case NodeKind::Submenu: return s.submenuForm;
    case NodeKind::Script: return s.scriptForm;
    case NodeKind::Html: return s.htmlForm;
    case NodeKind::Action: return s.actionForm;
    case NodeKind::Execute: return s.executeForm;
    case NodeKind::Separator: return s.separatorForm;
    default: return s.emptyForm;
    }
}

static void bindItemForm(EditorState& s, EditorNode* node)
{
    HWND form = itemFormFor(s, node);
    if (s.activeItemForm) ShowWindow(s.activeItemForm, SW_HIDE);
    for (HWND other : {s.emptyForm, s.scriptForm, s.htmlForm, s.actionForm, s.executeForm,
                       s.submenuForm, s.separatorForm})
        if (other && other != form) ShowWindow(other, SW_HIDE);
    s.activeItemForm = form;
    auto get = [form](int id) { return form ? GetDlgItem(form, id) : nullptr; };
    s.header = form ? GetDlgItem(form, ID_HEADER) : nullptr;
    s.titleLabel = get(ID_TITLE_LABEL); s.title = get(ID_TITLE);
    s.shortcutLabel = get(ID_SHORTCUT_LABEL); s.shortcut = get(ID_SHORTCUT);
    s.shortcutClear = get(ID_SHORTCUT_CLEAR);
    s.icon = get(ID_ICON);
    s.scriptLabel = get(ID_SCRIPT_LABEL); s.script = get(ID_SCRIPT);
    s.scriptBrowse = get(ID_SCRIPT_BROWSE); s.scriptEdit = get(ID_SCRIPT_EDIT);
    s.actionLabel = get(ID_ACTION_LABEL); s.action = get(ID_ACTION);
    s.actionTargetLabel = get(ID_ACTION_TARGET_LABEL); s.actionTarget = get(ID_ACTION_TARGET);
    s.actionHelp = get(ID_ACTION_HELP);
    if (s.actionHelp) SetWindowSubclass(s.actionHelp, helpLinkSubclass, 1, reinterpret_cast<DWORD_PTR>(&s));
    s.commandLabel = get(ID_COMMAND_LABEL); s.command = get(ID_COMMAND);
    s.commandHelp = get(ID_COMMAND_HELP); s.saveBeforeRun = get(ID_SAVE_BEFORE_RUN);
    s.runInLabel = get(ID_RUN_IN_LABEL); s.runIn = get(ID_RUN_IN);
    s.codePageLabel = get(ID_CODE_PAGE_LABEL); s.codePage = get(ID_CODE_PAGE);
    s.codePageHelp = get(ID_CODE_PAGE_HELP);
    s.envLabel = get(ID_ENV_LABEL); s.envList = get(ID_ENV_LIST); s.envHelp = get(ID_ENV_HELP);
    s.info = get(ID_INFO);
    if (s.scriptLabel) SetWindowTextW(s.scriptLabel,
        tr(s, node && node->kind == NodeKind::Html ? "ConfigHtmlFolder" : "ConfigScriptFile").c_str());

    if (s.shortcut) {
        SetWindowSubclass(s.shortcut, shortcutSubclass, 1, reinterpret_cast<DWORD_PTR>(&s));
    }
    if (s.commandHelp) SetWindowSubclass(s.commandHelp, helpLinkSubclass, 1, reinterpret_cast<DWORD_PTR>(&s));
    if (s.codePageHelp) SetWindowSubclass(s.codePageHelp, helpLinkSubclass, 1, reinterpret_cast<DWORD_PTR>(&s));
    if (s.envHelp) SetWindowSubclass(s.envHelp, helpLinkSubclass, 1, reinterpret_cast<DWORD_PTR>(&s));

    for (HWND combo : {s.action, s.runIn, s.codePage}) if (combo) {
        SendMessageW(combo, CB_SETITEMHEIGHT, 1, 25);
        SendMessageW(combo, CB_SETITEMHEIGHT, 0, 22);
    }
}

static void showTypeControls(EditorState& s, NodeKind kind, bool hasItem)
{
    const bool submenu = kind == NodeKind::Submenu || kind == NodeKind::TopMenu;
    const bool script = kind == NodeKind::Script || kind == NodeKind::Html;
    const bool action = kind == NodeKind::Action;
    const bool execute = kind == NodeKind::Execute;
    const bool separator = kind == NodeKind::Separator;
    show(s.icon, hasItem && s.toolbarItem && s.icon);
    if (s.header) {
        std::wstring heading;
        if (script) heading = tr(s, kind == NodeKind::Html ? "ConfigHtmlSettings" : "ConfigScriptSettings");
        else if (action) heading = tr(s, "ConfigActionSettings");
        else if (execute) heading = tr(s, "ConfigExecuteSettings");
        else if (submenu) heading = tr(s, "ConfigSubmenuSettings");
        else if (separator) heading = tr(s, "ConfigSeparator");
        else heading = tr(s, "ConfigItemSettings");
        SetWindowTextW(s.header, heading.c_str());
    }

}

static void showActionTargetControls(EditorState& s, bool visible, bool repaintPage = true)
{
    HWND form = s.actionTargetLabel ? GetParent(s.actionTargetLabel) : nullptr;
    if (!visible && s.actionTarget) SetWindowTextW(s.actionTarget, L"");
    for (HWND control : {s.actionTargetLabel, s.actionTarget, s.actionHelp}) {
        if (!control) continue;
        ShowWindow(control, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    EnableWindow(s.actionTarget, visible);
    if (repaintPage && form && s.activeItemForm == form) {
        // Tab/item changes hide the child page before the host background is
        // repainted. Mirror that sequence here so transparent static labels
        // and edit borders cannot leave stale pixels when the menu-path row hides.
        ShowWindow(form, SW_HIDE);
        showChildDialogAndRepaint(s.hwnd, form);
    }
}

static void updateHtmlAuthorizationOptions(EditorState& s, bool enabled)
{
    if (!enabled && s.htmlForm) {
        // Returning to the unchecked state must always reset the display-only
        // authorization choice to the first option.
        CheckRadioButton(s.htmlForm, ID_HTML_AUTH_RECONFIRM,
                         ID_HTML_AUTH_TRUST_FOLDER, ID_HTML_AUTH_RECONFIRM);
    }

    EnableWindow(s.htmlAuthReconfirm, enabled);
    EnableWindow(s.htmlAuthTrustFolder, enabled);
}

static HtmlPermissionGrant htmlPermissionGrant(cJSON* item)
{
    HtmlPermissionGrant grant;
    cJSON* object = member(item, "html_permissions");
    cJSON* mode = member(object, "mode");
    cJSON* folder = member(object, "folder");
    cJSON* digest = member(object, "sha256");
    if (!cJSON_IsObject(object) || !cJSON_IsString(mode) || !mode->valuestring ||
        !cJSON_IsString(folder) || !folder->valuestring) return grant;
    grant.mode = fromUtf8(mode->valuestring);
    grant.folder = fromUtf8(folder->valuestring);
    if (cJSON_IsString(digest) && digest->valuestring) grant.sha256 = fromUtf8(digest->valuestring);
    if (grant.mode != L"reconfirm" && grant.mode != L"trust_folder") return {};
    if ((grant.mode == L"reconfirm" && grant.sha256.size() != 64) ||
        (grant.mode == L"trust_folder" && !grant.sha256.empty())) return {};
    return grant;
}

static void invalidateHtmlAuthorizationIfFolderChanged(EditorState& s)
{
    if (s.updating || !s.selected || s.selected->kind != NodeKind::Html || !s.htmlAdvancedEnable ||
        IsDlgButtonChecked(s.htmlForm, ID_HTML_ADVANCED_ENABLE) != BST_CHECKED) return;
    const std::wstring folder = canonicalHtmlFolderPath(
        resolvedPath(s.workspaceFolder, readText(s.script)));
    if (!folder.empty() && !s.htmlAuthorizedFolder.empty() &&
        CompareStringOrdinal(folder.c_str(), -1, s.htmlAuthorizedFolder.c_str(), -1, TRUE) == CSTR_EQUAL)
        return;
    CheckDlgButton(s.htmlForm, ID_HTML_ADVANCED_ENABLE, BST_UNCHECKED);
    s.htmlAuthorizationConfirmed = false;
    s.htmlAuthorizedFolder.clear();
    s.htmlAuthorizedSha256.clear();
    updateHtmlAuthorizationOptions(s, false);
    s.dirty = true;
}

static INT_PTR CALLBACK htmlPermissionPreviewProc(HWND dialog, UINT message,
                                                 WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DESTROY) {
        HFONT emphasisFont = reinterpret_cast<HFONT>(RemovePropW(dialog, L"NppMenuScriptsHtmlPermissionEmphasisFont"));
        if (emphasisFont) DeleteObject(emphasisFont);
        return FALSE;
    }

    auto* s = reinterpret_cast<EditorState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        s = reinterpret_cast<EditorState*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(s));
        if (s) {
            SetWindowTextW(dialog, tr(*s, "HtmlPermDialogTitle").c_str());
            SetDlgItemTextW(dialog, ID_HTML_PERMISSION_HEADING, tr(*s, "HtmlPermHeading").c_str());
            SetDlgItemTextW(dialog, ID_HTML_PERMISSION_RISK, tr(*s, "HtmlPermRisk").c_str());
            SetDlgItemTextW(dialog, ID_HTML_PERMISSION_PROMPT, tr(*s, "HtmlPermTypePrompt").c_str());
            SetDlgItemTextW(dialog, ID_HTML_PERMISSION_PHRASE, tr(*s, "HtmlPermPhrase").c_str());
            SetDlgItemTextW(dialog, IDOK, tr(*s, "HtmlPermEnable").c_str());
            SetDlgItemTextW(dialog, IDCANCEL, tr(*s, "ConfigCancel").c_str());
            EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
            SendDlgItemMessageW(dialog, ID_HTML_PERMISSION_ICON, STM_SETICON,
                                reinterpret_cast<WPARAM>(LoadImageW(s->context.module,
                                    MAKEINTRESOURCEW(IDR_HTML_PERMISSION_ICON), IMAGE_ICON,
                                    32, 40, LR_DEFAULTCOLOR | LR_SHARED)), 0);

            RECT ownerRect{}, dialogRect{};
            if (s->hwnd && GetWindowRect(s->hwnd, &ownerRect) && GetWindowRect(dialog, &dialogRect)) {
                const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) -
                                                (dialogRect.right - dialogRect.left)) / 2;
                const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) -
                                               (dialogRect.bottom - dialogRect.top)) / 2;
                SetWindowPos(dialog, nullptr, x, y, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }

            LOGFONTW fontInfo{};
            HFONT dialogFont = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
            if (dialogFont && GetObjectW(dialogFont, sizeof(fontInfo), &fontInfo) == sizeof(fontInfo)) {
                fontInfo.lfWeight = FW_BOLD;
                HFONT emphasisFont = CreateFontIndirectW(&fontInfo);
                if (emphasisFont) {
                    SetPropW(dialog, L"NppMenuScriptsHtmlPermissionEmphasisFont", emphasisFont);
                    SendDlgItemMessageW(dialog, ID_HTML_PERMISSION_HEADING, WM_SETFONT,
                                        reinterpret_cast<WPARAM>(emphasisFont), TRUE);
                    SendDlgItemMessageW(dialog, ID_HTML_PERMISSION_PHRASE, WM_SETFONT,
                                        reinterpret_cast<WPARAM>(emphasisFont), TRUE);
                }
            }
        }
        return TRUE;
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        if (id == ID_HTML_PERMISSION_INPUT && HIWORD(wParam) == EN_CHANGE && s) {
            EnableWindow(GetDlgItem(dialog, IDOK),
                         readText(GetDlgItem(dialog, ID_HTML_PERMISSION_INPUT)) ==
                             tr(*s, "HtmlPermPhrase"));
            return TRUE;
        }
        if (id == IDOK || id == IDCANCEL) {
            if (id == IDOK && s &&
                readText(GetDlgItem(dialog, ID_HTML_PERMISSION_INPUT)) != tr(*s, "HtmlPermPhrase"))
                return TRUE;
            EndDialog(dialog, id);
            return TRUE;
        }
    }
    return FALSE;
}

static INT_PTR showHtmlPermissionPreview(EditorState& s)
{
    return DialogBoxParamW(s.context.module, MAKEINTRESOURCEW(IDD_HTML_PERMISSION_PREVIEW),
                           s.hwnd, htmlPermissionPreviewProc,
                           reinterpret_cast<LPARAM>(&s));
}

static void loadSelected(EditorState& s, EditorNode* node)
{
    s.updating = true;
    s.selected = node;
    s.toolbarItem = node && node->parent == s.toolbar && node->kind != NodeKind::ToolbarRoot;
    bindItemForm(s, node);
    const bool submenu = node && (node->kind == NodeKind::Submenu || node->kind == NodeKind::TopMenu);
    const bool separator = node && node->kind == NodeKind::Separator;
    const bool hasItem = node && node->kind != NodeKind::MenuRoot && node->kind != NodeKind::ToolbarRoot;
    showTypeControls(s, node ? node->kind : NodeKind::MenuRoot, hasItem);
    if (node && (node->kind == NodeKind::Script || node->kind == NodeKind::Html)) {
        if (node->kind == NodeKind::Html) {
            setRichHelpContent(s, s.htmlHelpText,
                {{tr(s, "ConfigHtmlHelp"), L""},
                 {tr(s, "ConfigHtmlPermissionsHelp"), s.context.htmlPermissionsUrl}}, 100);
            s.htmlAuthorizationConfirmed = false;
            s.htmlAuthorizedFolder.clear();
            s.htmlAuthorizedSha256.clear();
        } else {
            setRichHelpContent(s, s.scriptHelpText,
                {{tr(s, "ConfigScriptHelpRuntime"), s.context.scriptRequirementsUrl},
                 {tr(s, "ConfigScriptHelpCreate"), s.context.scriptTemplateUrl},
                 {tr(s, "ConfigScriptHelpApi"), s.context.scriptApiUrl}}, 100);
        }
    }
    std::wstring title;
    if (node && node->topMenu) title = fromUtf8(node->value->string);
    else if (node && hasItem && node->kind != NodeKind::Separator) {
        cJSON* titleItem = member(node->value, "title");
        if (cJSON_IsString(titleItem)) title = fromUtf8(titleItem->valuestring);
    }
    const auto titleParts = splitShortcut(title);
    if (s.title) SetWindowTextW(s.title, titleParts.first.c_str());
    if (s.shortcut) SetWindowTextW(s.shortcut, titleParts.second.c_str());
    showShortcutClear(s.shortcut, s.shortcutClear, !titleParts.second.empty());
    if (node && node->kind == NodeKind::Script) {
        cJSON* value = member(node->value, "script");
        SetWindowTextW(s.script, cJSON_IsString(value) ? fromUtf8(value->valuestring).c_str() : L"");
    }
    if (node && node->kind == NodeKind::Html) {
        cJSON* value = member(node->value, "html");
        SetWindowTextW(s.script, cJSON_IsString(value) ? fromUtf8(value->valuestring).c_str() : L"");
        const HtmlPermissionGrant grant = htmlPermissionGrant(node->value);
        const std::wstring htmlFolder = canonicalHtmlFolderPath(
            resolvedPath(s.workspaceFolder, cJSON_IsString(value) ? fromUtf8(value->valuestring) : L""));
        const bool grantValid = grant.enabled() && validateHtmlPermissionGrant(htmlFolder, grant);
        s.htmlAuthorizationConfirmed = grantValid;
        s.htmlAuthorizedFolder = grantValid ? htmlFolder : L"";
        s.htmlAuthorizedSha256 = grantValid && grant.mode == L"reconfirm" ? grant.sha256 : L"";
        CheckDlgButton(s.htmlForm, ID_HTML_ADVANCED_ENABLE,
                       grantValid ? BST_CHECKED : BST_UNCHECKED);
        CheckRadioButton(s.htmlForm, ID_HTML_AUTH_RECONFIRM, ID_HTML_AUTH_TRUST_FOLDER,
                         grant.mode == L"trust_folder" ? ID_HTML_AUTH_TRUST_FOLDER : ID_HTML_AUTH_RECONFIRM);
        updateHtmlAuthorizationOptions(s, grantValid);
    }
    if (node && node->kind == NodeKind::Action) {
        cJSON* value = member(node->value, "action");
        const std::wstring token = cJSON_IsString(value) ? fromUtf8(value->valuestring) : L"RUN_CURRENT_FILE";
        fillActionCombo(s, token);
        SetWindowTextW(s.actionTarget, token.rfind(L"MENU:", 0) == 0 ? token.substr(5).c_str() : L"");
        // loadSelected() repaints the active child page once after all fields
        // are populated, so avoid repainting it again while binding the row.
        showActionTargetControls(s, token.rfind(L"MENU:", 0) == 0, false);
    }
    if (node && node->kind == NodeKind::Execute) {
        cJSON* execute = member(node->value, "execute");
        cJSON* command = member(execute, "command");
        SetWindowTextW(s.command, cJSON_IsString(command) ? fromUtf8(command->valuestring).c_str() : L"");
        cJSON* save = member(execute, "save_before_run");
        SendMessageW(s.saveBeforeRun, BM_SETCHECK, cJSON_IsTrue(save) ? BST_CHECKED : BST_UNCHECKED, 0);
        cJSON* run = member(execute, "run_in");
        fillRunIn(s, cJSON_IsString(run) ? fromUtf8(run->valuestring) : L"output_pane");
        cJSON* codePage = member(execute, "code_page");
        const std::wstring codePageText = cJSON_IsString(codePage) ? fromUtf8(codePage->valuestring) : L"ANSI";
        const LRESULT codePageIndex = SendMessageW(s.codePage, CB_FINDSTRINGEXACT,
                                                    static_cast<WPARAM>(-1),
                                                    reinterpret_cast<LPARAM>(codePageText.c_str()));
        if (codePageIndex != CB_ERR) SendMessageW(s.codePage, CB_SETCURSEL, codePageIndex, 0);
        else SetWindowTextW(s.codePage, codePageText.c_str());
        loadEnvironmentText(s, node);
    } else {
        s.environment.clear();
    }
    // Reset the previous toolbar item's icon before loading this item's icon.
    s.currentIconPath.clear();
    if (s.toolbarItem) {
        cJSON* icon = member(node->value, "icon");
        if (cJSON_IsString(icon)) s.currentIconPath = fromUtf8(icon->valuestring);
    }
    updateIconPreview(s);
    const bool canCreate = node && (node->kind == NodeKind::MenuRoot || node->kind == NodeKind::ToolbarRoot ||
        (submenu && node->parent != s.toolbar));
    EnableWindow(s.newButton, canCreate);
    EnableWindow(s.deleteButton, hasItem);
    bool hasPrevious = false, hasNext = false;
    if (node && node->parent && node->kind != NodeKind::MenuRoot && node->kind != NodeKind::ToolbarRoot) {
        if (node->topMenu) { hasPrevious = node->value->prev != nullptr; hasNext = node->value->next != nullptr; }
        else { hasPrevious = node->value->prev != nullptr; hasNext = node->value->next != nullptr; }
    }
    EnableWindow(s.upButton, hasPrevious);
    EnableWindow(s.downButton, hasNext);
    updateRunButton(s);
    if (!hasItem) setRichPlainText(s, s.info, tr(s, node && node->kind == NodeKind::ToolbarRoot ? "ConfigSelectToolbar" : "ConfigSelectMenu"));
    else if (separator) setRichPlainText(s, s.info, tr(s, "ConfigSeparatorInfo"));
    s.updating = false;

    if (s.activePage == 0 && s.activeItemForm)
        showChildDialogAndRepaint(s.hwnd, s.activeItemForm);
}

static bool setObjectString(cJSON* object, const char* key, const std::wstring& value)
{
    return setString(object, key, value);
}

static bool commitSelected(EditorState& s);

static bool commitSelected(EditorState& s)
{
    EditorNode* node = s.selected;
    if (!node || node->kind == NodeKind::MenuRoot || node->kind == NodeKind::ToolbarRoot || node->kind == NodeKind::Separator) return true;
    if (node->topMenu) {
        std::wstring title = readText(s.title);
        if (toUtf8(title) != (node->value->string ? node->value->string : "")) setPropertyName(node->value, title);
        return true;
    }
    std::wstring title = readText(s.title);
    const std::wstring shortcut = readText(s.shortcut);
    if (!shortcut.empty()) {
        title.push_back(L'\t');
        title += shortcut;
    }
    if (!setObjectString(node->value, "title", title)) return false;
    if (node->kind == NodeKind::Script) {
        if (!setObjectString(node->value, "script", storedScriptPath(s.scriptFolder, readText(s.script)))) return false;
    } else if (node->kind == NodeKind::Html) {
        const std::wstring folder = resolvedPath(s.workspaceFolder, readText(s.script));
        if (!setObjectString(node->value, "html", storedPath(s.workspaceFolder, folder))) return false;
        const bool authorized = IsDlgButtonChecked(s.htmlForm, ID_HTML_ADVANCED_ENABLE) == BST_CHECKED;
        if (!authorized) {
            cJSON_DeleteItemFromObjectCaseSensitive(node->value, "html_permissions");
            s.htmlAuthorizationConfirmed = false;
            s.htmlAuthorizedFolder.clear();
            s.htmlAuthorizedSha256.clear();
        } else {
            const std::wstring canonicalFolder = canonicalHtmlFolderPath(folder);
            if (!s.htmlAuthorizationConfirmed || canonicalFolder.empty() ||
                s.htmlAuthorizedFolder.empty() ||
                CompareStringOrdinal(canonicalFolder.c_str(), -1, s.htmlAuthorizedFolder.c_str(), -1, TRUE) != CSTR_EQUAL ||
                !htmlFolderHasIndex(canonicalFolder)) {
                s.validationError = tr(s, "ConfigHtmlIndexMissing");
                return false;
            }
            const bool trustFolder = IsDlgButtonChecked(s.htmlForm, ID_HTML_AUTH_TRUST_FOLDER) == BST_CHECKED;
            HtmlPermissionGrant grant;
            grant.mode = trustFolder ? L"trust_folder" : L"reconfirm";
            grant.folder = canonicalFolder;
            if (!trustFolder && (s.htmlAuthorizedSha256.empty() ||
                !htmlFolderSha256(canonicalFolder, grant.sha256) ||
                CompareStringOrdinal(grant.sha256.c_str(), -1, s.htmlAuthorizedSha256.c_str(), -1, TRUE) != CSTR_EQUAL)) {
                s.validationError = tr(s, "ConfigHtmlIndexMissing");
                return false;
            }
            cJSON_DeleteItemFromObjectCaseSensitive(node->value, "html_permissions");
            cJSON* permissions = cJSON_AddObjectToObject(node->value, "html_permissions");
            if (!permissions || !setObjectString(permissions, "mode", grant.mode) ||
                !setObjectString(permissions, "folder", grant.folder) ||
                (!trustFolder && !setObjectString(permissions, "sha256", grant.sha256))) return false;
        }
    } else if (node->kind == NodeKind::Action) {
        const LRESULT index = SendMessageW(s.action, CB_GETCURSEL, 0, 0);
        const int actionKind = index == CB_ERR ? 1 : static_cast<int>(SendMessageW(s.action, CB_GETITEMDATA, index, 0));
        std::wstring token;
        if (actionKind == 1) token = L"RUN_CURRENT_FILE";
        else if (actionKind == 2) token = L"STOP_SCRIPT";
        else if (actionKind == 3) token = L"MENU:" + readText(s.actionTarget);
        else token = L"CONFIGURE_MENU";
        if (!setObjectString(node->value, "action", token)) return false;
    } else if (node->kind == NodeKind::Execute) {
        cJSON* execute = member(node->value, "execute");
        if (!cJSON_IsObject(execute)) {
            execute = cJSON_CreateObject();
            if (!execute || !cJSON_AddItemToObject(node->value, "execute", execute)) return false;
        }
        if (!setObjectString(execute, "command", readText(s.command)) ||
            !setBool(execute, "save_before_run", SendMessageW(s.saveBeforeRun, BM_GETCHECK, 0, 0) == BST_CHECKED) ||
            !setObjectString(execute, "run_in", selectedRunIn(s)) ||
            !setObjectString(execute, "code_page", readText(s.codePage))) return false;
        if (!node->environmentTextLoaded) {
            node->environmentText.clear();
            node->environmentTextLoaded = true;
        }
        s.environment = parseEnvironmentText(node->environmentText);
        cJSON_DeleteItemFromObjectCaseSensitive(execute, "env");
        cJSON* env = cJSON_AddObjectToObject(execute, "env");
        if (!env) return false;
        for (const auto& pair : s.environment) {
            const std::string key = toUtf8(pair.first), value = toUtf8(pair.second);
            if (!cJSON_AddStringToObject(env, key.c_str(), value.c_str())) return false;
        }
    }
    if (s.toolbarItem) {
        if (s.currentIconPath.empty()) cJSON_DeleteItemFromObjectCaseSensitive(node->value, "icon");
        else if (!setObjectString(node->value, "icon", s.currentIconPath)) return false;
    }
    return true;
}

static bool validateItems(EditorState& s, cJSON* items, unsigned int depth,
                          const std::wstring& parentPath, std::wstring& error)
{
    if (!cJSON_IsArray(items) || depth > 16) {
        error = tr(s, "ConfigInvalidJson") + L"\n\n" + parentPath;
        return false;
    }
    for (cJSON* item = items->child; item; item = item->next) {
        const std::wstring itemTitle = cJSON_IsString(member(item, "title"))
            ? fromUtf8(member(item, "title")->valuestring) : L"(untitled item)";
        const std::wstring itemPath = parentPath + L" > " + itemTitle;
        if (!cJSON_IsObject(item)) {
            error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath;
            return false;
        }
        if (!item->child) continue;
        cJSON* title = member(item, "title");
        cJSON* script = member(item, "script");
        cJSON* html = member(item, "html");
        cJSON* action = member(item, "action");
        cJSON* execute = member(item, "execute");
        cJSON* sub = member(item, "sub");
        if (sub) {
            if (!cJSON_IsString(title) || !title->valuestring || !title->valuestring[0] || !cJSON_IsArray(sub) || script || html || action || execute || depth == 16) {
                error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath;
                return false;
            }
            if (!validateItems(s, sub, depth + 1, itemPath, error)) return false;
        } else {
            if (!cJSON_IsString(title) || !title->valuestring || !title->valuestring[0] || static_cast<int>(!!script) + static_cast<int>(!!html) + static_cast<int>(!!action) + static_cast<int>(!!execute) != 1) {
                error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath;
                return false;
            }
            if (script) {
                if (!cJSON_IsString(script) || !script->valuestring || !*script->valuestring) {
                    error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath;
                    return false;
                }
                const std::wstring path = fromUtf8(script->valuestring);
                const size_t dot = path.find_last_of(L'.');
                if (dot == std::wstring::npos) {
                    error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath;
                    return false;
                }
                const std::wstring ext = path.substr(dot);
                if (_wcsicmp(ext.c_str(), L".js") && _wcsicmp(ext.c_str(), L".mjs") && _wcsicmp(ext.c_str(), L".pl") && _wcsicmp(ext.c_str(), L".py")) {
                    error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath;
                    return false;
                }
            } else if (html) {
                if (!cJSON_IsString(html) || !html->valuestring || !*html->valuestring) {
                    error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath;
                    return false;
                }
                const std::wstring folder = resolvedPath(s.workspaceFolder, fromUtf8(html->valuestring));
                if (!htmlFolderHasIndex(folder)) {
                    error = tr(s, "ConfigHtmlIndexMissing") + L"\n\n" + itemPath;
                    return false;
                }
            } else if (action) {
                if (!cJSON_IsString(action) || !action->valuestring) {
                    error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath;
                    return false;
                }
                const std::wstring token = fromUtf8(action->valuestring);
                if (token != L"RUN_CURRENT_FILE" && token != L"STOP_SCRIPT" && token != L"CONFIGURE_MENU" && token.rfind(L"MENU:", 0) != 0) {
                    error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath;
                    return false;
                }
                if (token.rfind(L"MENU:", 0) == 0 && token.size() <= 5) {
                    error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath;
                    return false;
                }
            } else {
                cJSON* command = member(execute, "command");
                cJSON* run = member(execute, "run_in");
                if (!cJSON_IsObject(execute)) {
                    error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath;
                    return false;
                }
                if (!cJSON_IsString(command) || !command->valuestring || !command->valuestring[0]) {
                    error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath +
                            L"\n\nexecute.command must be a non-empty string.";
                    return false;
                }
                if (!cJSON_IsString(run)) {
                    error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath +
                            L"\n\nexecute.run_in must be a string.";
                    return false;
                }
                const std::wstring runValue = fromUtf8(run->valuestring);
                if (runValue != L"output_pane" && runValue != L"console" && runValue != L"windows") {
                    error = tr(s, "ConfigInvalidJson") + L"\n\n" + itemPath +
                            L"\n\nUnsupported execute.run_in value: " + runValue;
                    return false;
                }
            }
        }
    }
    return true;
}

static bool validateAndCommit(EditorState& s, std::wstring& error)
{
    s.validationError.clear();
    if (!commitSelected(s)) { error = s.validationError.empty() ? tr(s, "ConfigDuplicateMenuTitle") : s.validationError; return false; }
    if (!cJSON_IsObject(s.root) || !cJSON_IsObject(s.menu)) { error = tr(s, "ConfigInvalidJson"); return false; }
    for (cJSON* group = s.menu->child; group; group = group->next) {
        for (cJSON* other = group->next; other; other = other->next)
            if (group->string && other->string && std::strcmp(group->string, other->string) == 0) {
                error = tr(s, "ConfigDuplicateMenuTitle"); return false;
            }
        const std::wstring groupPath = L"Menu > " + (group->string ? fromUtf8(group->string) : L"(unnamed menu)");
        if (!group->string || !validateItems(s, group, 0, groupPath, error)) {
            if (error.empty()) error = tr(s, "ConfigInvalidJson") + L"\n\n" + groupPath;
            return false;
        }
    }
    if (s.toolbar && !cJSON_IsArray(s.toolbar)) { error = tr(s, "ConfigInvalidJson"); return false; }
    if (s.toolbar && cJSON_GetArraySize(s.toolbar) > 32) { error = tr(s, "ConfigInvalidJson"); return false; }
    for (cJSON* item = s.toolbar ? s.toolbar->child : nullptr; item; item = item->next) {
        if (!cJSON_IsObject(item) || !cJSON_IsString(member(item, "title"))) { error = tr(s, "ConfigInvalidJson"); return false; }
        cJSON* script = member(item, "script"), *action = member(item, "action"), *execute = member(item, "execute"), *html = member(item, "html");
        if (static_cast<int>(!!script) + static_cast<int>(!!action) + static_cast<int>(!!execute) + static_cast<int>(!!html) != 1) { error = tr(s, "ConfigInvalidJson"); return false; }
        if (html && (!cJSON_IsString(html) || !html->valuestring || !html->valuestring[0])) { error = tr(s, "ConfigInvalidJson"); return false; }
        if (html && !htmlFolderHasIndex(resolvedPath(s.workspaceFolder, fromUtf8(html->valuestring)))) {
            error = tr(s, "ConfigHtmlIndexMissing"); return false;
        }
        cJSON* icon = member(item, "icon");
        if (icon) {
            if (!cJSON_IsString(icon)) { error = tr(s, "ConfigInvalidJson"); return false; }
            std::wstring iconPath;
            int iconIndex = 0;
            const std::wstring iconValue = fromUtf8(icon->valuestring);
            const bool indexed = splitIconLocation(iconValue, iconPath, iconIndex);
            const std::wstring resolved = resolvedPath(s.iconFolder, iconPath);
            HICON check = nullptr;
            if (indexed) check = loadIconLocation(L"", resolved + L"," + std::to_wstring(iconIndex), 16, 16);
            else if (iconPath.size() >= 4 && _wcsicmp(iconPath.c_str() + iconPath.size() - 4, L".ico") == 0 && fileExists(resolved))
                check = static_cast<HICON>(LoadImageW(nullptr, resolved.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));
            if (!check) {
                error = tr(s, "ConfigIconMissing"); return false;
            }
            DestroyIcon(check);
        }
    }
    if (!commitHideMenuOptions(s)) { error = tr(s, "ConfigSaveFailed"); return false; }
    if (s.context.validate && !s.context.validate(s.root, error)) return false;
    return true;
}

static bool saveFile(EditorState& s, std::wstring& error)
{
    if (!validateAndCommit(s, error)) return false;
    char* printed = cJSON_Print(s.root);
    if (!printed) { error = tr(s, "ConfigSaveFailed"); return false; }
    const std::string bytes = std::string("\xEF\xBB\xBF") + printed;
    std::free(printed);
    const std::wstring tempPath = s.context.menuPath + L".tmp";
    HANDLE file = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = tr(s, "ConfigSaveFailed"); return false; }
    DWORD written = 0;
    const bool wrote = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!wrote || !MoveFileExW(tempPath.c_str(), s.context.menuPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str()); error = tr(s, "ConfigSaveFailed"); return false;
    }
    s.dirty = false;
    return true;
}

static bool saveAndRebuild(EditorState& s, std::wstring& error)
{
    if (!s.context.saveAndRebuild) {
        error = tr(s, "ConfigRebuildFailed");
        return false;
    }
    std::wstring status;
    if (!s.context.saveAndRebuild(error, status)) return false;
    if (s.statusBar) {
        s.statusText = std::move(status);
        SendMessageW(s.statusBar, SB_SETTEXTW, SBT_OWNERDRAW, 0);
        UpdateWindow(s.statusBar);
    }
    return true;
}

static void clearEditorStatus(EditorState& s)
{
    if (!s.statusBar) return;
    s.statusText.clear();
    SendMessageW(s.statusBar, SB_SETTEXTW, SBT_OWNERDRAW, 0);
    UpdateWindow(s.statusBar);
}

static int dialogUnitsToPixelsX(HWND dialog, int units)
{
    RECT rect{0, 0, units, 0};
    if (!dialog || !MapDialogRect(dialog, &rect)) return units;
    return rect.right;
}

static bool drawEditorStatus(EditorState& s, const DRAWITEMSTRUCT& draw)
{
    if (draw.CtlID != ID_EDITOR_STATUS || draw.hwndItem != s.statusBar) return false;
    const int saved = SaveDC(draw.hDC);
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, GetSysColor(COLOR_WINDOWTEXT));
    HGDIOBJ oldFont = s.font ? SelectObject(draw.hDC, s.font) : nullptr;
    RECT textRect = draw.rcItem;
    textRect.left += dialogUnitsToPixelsX(s.hwnd, 6);
    DrawTextW(draw.hDC, s.statusText.c_str(), static_cast<int>(s.statusText.size()), &textRect,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (oldFont) SelectObject(draw.hDC, oldFont);
    if (saved) RestoreDC(draw.hDC, saved);
    return true;
}

static void showSaveError(EditorState& s, const std::wstring& error)
{
    if (!error.empty()) MessageBoxW(s.hwnd, error.c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONERROR);
}

static std::wstring browseFolder(EditorState& s, const std::wstring& initial, const char* titleKey);

static void chooseScriptFile(EditorState& s)
{
    const bool html = s.selected && s.selected->kind == NodeKind::Html;
    if (html) {
        const std::wstring current = readText(s.script);
        const std::wstring initial = current.empty() ? s.workspaceFolder : resolvedPath(s.workspaceFolder, current);
        const std::wstring folder = browseFolder(s, initial, "ConfigHtmlFolderBrowse");
        if (folder.empty()) return;
        if (!htmlFolderHasIndex(folder)) {
            MessageBoxW(s.hwnd, tr(s, "ConfigHtmlIndexMissing").c_str(),
                        tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING);
            return;
        }
        SetWindowTextW(s.script, storedPath(s.workspaceFolder, folder).c_str());
        s.dirty = true;
        return;
    }
    wchar_t file[32768]{};
    OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = s.hwnd;
    ofn.lpstrFilter = L"Supported scripts (*.js;*.mjs;*.pl;*.py)\0*.js;*.mjs;*.pl;*.py\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = file; ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.lpstrInitialDir = s.scriptFolder.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) {
        const std::wstring selected = storedScriptPath(s.scriptFolder, file);
        SetWindowTextW(s.script, selected.c_str());
        s.dirty = true;
    }
}

static void editScriptFile(EditorState& s)
{
    const std::wstring scriptPath = readText(s.script);
    const bool html = s.selected && s.selected->kind == NodeKind::Html;
    std::wstring path = resolvedPath(html ? s.workspaceFolder : s.scriptFolder, scriptPath);
    if (scriptPath.empty()) {
        MessageBoxW(s.hwnd, tr(s, html ? "ConfigHtmlIndexMissing" : "ConfigScriptFileMissing").c_str(),
                    tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    if (!html && s.context.openOrCreateScript) {
        s.context.openOrCreateScript(s.hwnd, path);
        return;
    }
    if (html) {
        if (!htmlFolderHasIndex(path)) {
            MessageBoxW(s.hwnd, tr(s, "ConfigHtmlIndexMissing").c_str(),
                        tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING);
            return;
        }
        path = joinPath(path, L"index.html");
    }
    if (!fileExists(path)) {
        MessageBoxW(s.hwnd, tr(s, html ? "ConfigHtmlIndexMissing" : "ConfigScriptFileMissing").c_str(),
                    tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    SendMessageW(s.context.owner, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(path.c_str()));
}

static void chooseIcon(EditorState& s)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, tr(s, "ConfigChooseIcon").c_str());
    AppendMenuW(menu, MF_STRING, 3, tr(s, "ConfigChooseWindowsIcon").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, tr(s, "ConfigUseDefaultIcon").c_str());
    RECT rect{}; GetWindowRect(s.icon, &rect);
    const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, rect.left, rect.bottom, 0, s.hwnd, nullptr);
    DestroyMenu(menu);
    if (choice == 2) { s.currentIconPath.clear(); if (s.customIcon) { DestroyIcon(s.customIcon); s.customIcon = nullptr; } updateIconPreview(s); s.dirty = true; return; }
    if (choice == 3) {
        wchar_t systemDir[MAX_PATH + 1]{};
        const UINT length = GetSystemDirectoryW(systemDir, static_cast<UINT>(std::size(systemDir)));
        if (!length || length >= std::size(systemDir)) return;
        std::wstring initialPath(systemDir, length);
        initialPath += L"\\shell32.dll";
        wchar_t selectedPath[MAX_PATH]{};
        wcsncpy_s(selectedPath, initialPath.c_str(), _TRUNCATE);
        int selectedIndex = 0;
        if (!PickIconDlg(s.hwnd, selectedPath, static_cast<UINT>(std::size(selectedPath)), &selectedIndex)) return;
        const std::wstring selected = fullPath(selectedPath);
        const std::wstring spec = selected + L"," + std::to_wstring(selectedIndex);
        HICON loaded = loadIconLocation(L"", spec, 32, 32);
        if (!loaded) { MessageBoxW(s.hwnd, tr(s, "ConfigIconMissing").c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING); return; }
        if (s.customIcon) DestroyIcon(s.customIcon);
        s.customIcon = loaded;
        s.iconPreview = loaded;
        s.currentIconPath = spec;
        InvalidateRect(s.icon, nullptr, TRUE);
        s.dirty = true;
        return;
    }
    if (choice != 1) return;
    wchar_t file[32768]{};
    OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = s.hwnd;
    ofn.lpstrFilter = L"Icon files (*.ico)\0*.ico\0\0"; ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file)); ofn.lpstrInitialDir = s.iconFolder.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;
    const std::wstring selected = fullPath(file);
    if (selected.size() < 4 || _wcsicmp(selected.c_str() + selected.size() - 4, L".ico") != 0) {
        MessageBoxW(s.hwnd, tr(s, "ConfigIconMissing").c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING); return;
    }
    HICON loaded = static_cast<HICON>(LoadImageW(nullptr, selected.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
    if (!loaded) { MessageBoxW(s.hwnd, tr(s, "ConfigIconMissing").c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING); return; }
    if (s.customIcon) DestroyIcon(s.customIcon);
    s.customIcon = loaded;
    s.iconPreview = loaded;
    s.currentIconPath = storedPath(s.iconFolder, selected);
    InvalidateRect(s.icon, nullptr, TRUE);
    s.dirty = true;
}

static bool commitSelected(EditorState& s);

static bool addItem(EditorState& s, int choice)
{
    if (!s.selected || !IsWindowEnabled(s.newButton)) return false;
    if (!commitSelected(s)) return false;
    EditorNode* selected = s.selected;
    if (selected->kind == NodeKind::MenuRoot) {
        if (choice != ID_NEW_SUBMENU) return false;
        std::string menuName = toUtf8(tr(s, "ConfigNewMenuDefault"));
        if (menuName.empty()) menuName = "New Menu";
        for (unsigned int suffix = 2; member(s.menu, menuName.c_str()); ++suffix)
            menuName = toUtf8(tr(s, "ConfigNewMenuDefault")) + " " + std::to_string(suffix);
        cJSON* items = cJSON_CreateArray();
        if (!items || !cJSON_AddItemToObject(s.menu, menuName.c_str(), items)) { cJSON_Delete(items); return false; }
        s.dirty = true;
        rebuildTree(s, items);
        return true;
    }
    if (selected->kind == NodeKind::ToolbarRoot && choice != ID_NEW_SCRIPT && choice != ID_NEW_EXECUTE &&
        choice != ID_NEW_ACTION && choice != ID_NEW_HTML) return false;
    if (selected->kind != NodeKind::ToolbarRoot && selected->kind != NodeKind::TopMenu && selected->kind != NodeKind::Submenu) return false;
    if (selected->kind != NodeKind::ToolbarRoot && choice == ID_NEW_SUBMENU && selected->depth >= 17) return false;
    cJSON* selectedArray = selected->kind == NodeKind::ToolbarRoot ? s.toolbar :
        selected->kind == NodeKind::TopMenu ? selected->value : member(selected->value, "sub");
    if (selected->kind == NodeKind::ToolbarRoot && !selectedArray) {
        selectedArray = cJSON_AddArrayToObject(s.root, "toolbar");
        s.toolbar = selectedArray;
    }
    if (!selectedArray) return false;
    cJSON* newItem = nullptr;
    if (choice == ID_NEW_SEPARATOR) newItem = cJSON_CreateObject();
    else if (choice == ID_NEW_SUBMENU) {
        newItem = cJSON_CreateObject();
        if (newItem) {
            const std::string defaultTitle = toUtf8(tr(s, "ConfigNewSubmenuDefault"));
            cJSON_AddStringToObject(newItem, "title", defaultTitle.empty() ? "New Submenu" : defaultTitle.c_str());
            cJSON_AddArrayToObject(newItem, "sub");
        }
    } else {
        newItem = cJSON_CreateObject();
        if (!newItem) return false;
        const std::string defaultTitle = toUtf8(tr(s, "ConfigNewItemDefault"));
        cJSON_AddStringToObject(newItem, "title", defaultTitle.empty() ? "New Item" : defaultTitle.c_str());
        if (choice == ID_NEW_SCRIPT) cJSON_AddStringToObject(newItem, "script", "");
        else if (choice == ID_NEW_HTML) cJSON_AddStringToObject(newItem, "html", "");
        else if (choice == ID_NEW_ACTION) cJSON_AddStringToObject(newItem, "action", "RUN_CURRENT_FILE");
        else if (choice == ID_NEW_EXECUTE) {
            cJSON* execute = cJSON_AddObjectToObject(newItem, "execute");
            cJSON_AddStringToObject(execute, "command", "");
            cJSON_AddBoolToObject(execute, "save_before_run", 0);
            cJSON_AddStringToObject(execute, "code_page", "ANSI");
            cJSON_AddStringToObject(execute, "run_in", "output_pane");
            cJSON_AddObjectToObject(execute, "env");
        }
    }
    if (!newItem) return false;
    if (!cJSON_AddItemToArray(selectedArray, newItem)) { cJSON_Delete(newItem); return false; }
    s.dirty = true;
    rebuildTree(s, newItem);
    return true;
}

static bool reorderLinked(cJSON* parent, cJSON* item, bool up)
{
    if (!parent || !item) return false;
    cJSON* left = up ? item->prev : item;
    cJSON* right = up ? item : item->next;
    if (!left || !right) return false;
    cJSON* before = left->prev;
    cJSON* after = right->next;
    if (before) before->next = right; else parent->child = right;
    right->prev = before;
    right->next = left;
    left->prev = right;
    left->next = after;
    if (after) after->prev = left;
    return true;
}

static void moveSelected(EditorState& s, bool up)
{
    EditorNode* node = s.selected;
    if (!node || !node->parent || !commitSelected(s)) return;
    bool moved = false;
    if (node->topMenu) moved = reorderLinked(node->parent, node->value, up);
    else {
        const int count = cJSON_GetArraySize(node->parent);
        int index = -1;
        for (int i = 0; i < count; ++i) if (cJSON_GetArrayItem(node->parent, i) == node->value) { index = i; break; }
        const int target = index + (up ? -1 : 1);
        if (index >= 0 && target >= 0 && target < count) {
            cJSON* detached = cJSON_DetachItemFromArray(node->parent, index);
            moved = detached && cJSON_InsertItemInArray(node->parent, target, detached);
        }
    }
    if (moved) { s.dirty = true; rebuildTree(s, node->value); }
}

static void deleteSelected(EditorState& s)
{
    EditorNode* node = s.selected;
    if (!node || !node->parent || node->kind == NodeKind::MenuRoot || node->kind == NodeKind::ToolbarRoot || !commitSelected(s)) return;
    if (MessageBoxW(s.hwnd, tr(s, "ConfigDeleteConfirm").c_str(), tr(s, "ConfigTitle").c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    cJSON* parent = node->parent;
    cJSON* selection = parent == s.menu || parent == s.toolbar ? parent : nullptr;
    for (const auto& candidate : s.nodes) {
        if (!selection && candidate.get() != node && candidate->kind == NodeKind::Submenu && member(candidate->value, "sub") == parent)
            selection = candidate->value;
        if (!selection && candidate.get() != node && candidate->kind == NodeKind::TopMenu && candidate->value == parent)
            selection = candidate->value;
    }
    cJSON* detached = cJSON_DetachItemViaPointer(parent, node->value);
    if (detached) cJSON_Delete(detached);
    s.dirty = true;
    rebuildTree(s, selection);
}

static void showTreeContextMenu(EditorState& s, POINT screenPoint)
{
    POINT client = screenPoint;
    ScreenToClient(s.tree, &client);
    TVHITTESTINFO hit{};
    hit.pt = client;
    const HTREEITEM item = TreeView_HitTest(s.tree, &hit);
    if (!item) return;

    if (TreeView_GetSelection(s.tree) != item) {
        if (!commitSelected(s)) {
            MessageBoxW(s.hwnd, tr(s, "ConfigDuplicateMenuTitle").c_str(), tr(s, "ConfigTitle").c_str(),
                        MB_OK | MB_ICONWARNING);
            return;
        }
        TreeView_SelectItem(s.tree, item);
    }

    EditorNode* node = treeNode(s, item);
    if (!node) return;
    const bool menuRoot = node->kind == NodeKind::MenuRoot;
    const bool toolbarRoot = node->kind == NodeKind::ToolbarRoot;
    const bool hasItem = node->parent && !menuRoot && !toolbarRoot;
    const bool canCreate = IsWindowEnabled(s.newButton) != FALSE;
    const bool canRun = !s.dirty && isRunnableNode(node->kind) && node->commandIndex >= 0 &&
                        static_cast<bool>(s.context.runSelectedItem);

    HMENU contextMenu = CreatePopupMenu();
    HMENU newMenu = CreatePopupMenu();
    if (!contextMenu || !newMenu) {
        if (contextMenu) DestroyMenu(contextMenu);
        if (newMenu) DestroyMenu(newMenu);
        return;
    }
    AppendMenuW(newMenu, MF_STRING | (menuRoot ? MF_GRAYED : 0), ID_NEW_SCRIPT, tr(s, "ConfigScript").c_str());
    AppendMenuW(newMenu, MF_STRING | (menuRoot ? MF_GRAYED : 0), ID_NEW_HTML, tr(s, "ConfigHtml").c_str());
    AppendMenuW(newMenu, MF_STRING | (menuRoot ? MF_GRAYED : 0), ID_NEW_EXECUTE, tr(s, "ConfigExecute").c_str());
    AppendMenuW(newMenu, MF_STRING | (menuRoot ? MF_GRAYED : 0), ID_NEW_ACTION, tr(s, "ConfigAction").c_str());
    AppendMenuW(newMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(newMenu, MF_STRING | ((menuRoot || toolbarRoot) ? MF_GRAYED : 0),
                ID_NEW_SEPARATOR, tr(s, "ConfigSeparator").c_str());
    const bool submenuDisabled = toolbarRoot || (node && node->depth >= 17);
    AppendMenuW(newMenu, MF_STRING | (submenuDisabled ? MF_GRAYED : 0),
                ID_NEW_SUBMENU, tr(s, "ConfigSubmenu").c_str());
    AppendMenuW(contextMenu, MF_POPUP | (canCreate ? 0 : MF_GRAYED),
                reinterpret_cast<UINT_PTR>(newMenu), tr(s, "ConfigNew").c_str());
    AppendMenuW(contextMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(contextMenu, MF_STRING | (canRun ? 0 : MF_GRAYED), ID_RUN,
                tr(s, "ConfigRun").c_str());
    AppendMenuW(contextMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(contextMenu, MF_STRING | (hasItem ? 0 : MF_GRAYED), ID_DELETE,
                tr(s, "ConfigDelete").c_str());

    SetForegroundWindow(s.hwnd);
    const UINT command = TrackPopupMenu(contextMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                        screenPoint.x, screenPoint.y, 0, s.hwnd, nullptr);
    DestroyMenu(contextMenu);
    if (command == ID_RUN && canRun)
        s.context.runSelectedItem(static_cast<size_t>(node->commandIndex));
    else if (command == ID_DELETE) deleteSelected(s);
    else if (command >= ID_NEW_SCRIPT && command <= ID_NEW_HTML)
        addItem(s, static_cast<int>(command));
}

static void addEnvironment(EditorState& s)
{
    const std::wstring name = readText(s.envName), value = readText(s.envValue);
    if (name.empty() || name.find(L'=') != std::wstring::npos) return;
    s.environment[name] = value;
    refreshEnvironment(s);
    s.dirty = true;
}

static int CALLBACK folderBrowseCallback(HWND hwnd, UINT message, LPARAM, LPARAM data)
{
    if (message == BFFM_INITIALIZED && data)
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
    return FALSE;
}

static LRESULT CALLBACK shortcutSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR, DWORD_PTR data)
{
    auto* state = reinterpret_cast<EditorState*>(data);
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && state) {
        const UINT key = static_cast<UINT>(wParam);
        if (key == VK_ESCAPE) {
            SetWindowTextW(hwnd, L"");
            showShortcutClear(hwnd, state->shortcutClear, false);
            state->dirty = true;
            PostMessageW(state->hwnd, TITLE_REFRESH_MESSAGE, 0, 0);
            return 0;
        }
        if (key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU) return 0;
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool validKey = (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || (key >= VK_F1 && key <= VK_F24);
        if (!validKey || (!ctrl && !alt && !shift)) {
            MessageBoxW(state->hwnd, tr(*state, "ConfigInvalidShortcut").c_str(), tr(*state, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING);
            return 0;
        }
        std::wstring text;
        if (ctrl) text += L"Ctrl+";
        if (alt) text += L"Alt+";
        if (shift) text += L"Shift+";
        if (key >= VK_F1 && key <= VK_F24) text += L"F" + std::to_wstring(key - VK_F1 + 1);
        else text += static_cast<wchar_t>(key);
        SetWindowTextW(hwnd, text.c_str());
        showShortcutClear(hwnd, state->shortcutClear, true);
        state->dirty = true;
        PostMessageW(state->hwnd, TITLE_REFRESH_MESSAGE, 0, 0);
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, shortcutSubclass, 1);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

static LRESULT CALLBACK shortcutClearHoverSubclass(HWND hwnd, UINT message, WPARAM wParam,
                                                   LPARAM lParam, UINT_PTR, DWORD_PTR data)
{
    auto* state = reinterpret_cast<EditorState*>(data);
    if (message == WM_MOUSEMOVE && state && state->shortcutClearHovered != hwnd) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tracking);
        state->shortcutClearHovered = hwnd;
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (message == WM_MOUSELEAVE && state && state->shortcutClearHovered == hwnd) {
        state->shortcutClearHovered = nullptr;
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, shortcutClearHoverSubclass, 2);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

static LRESULT CALLBACK helpLinkSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR subclassId, DWORD_PTR data)
{
    (void)data;
    if (message == WM_SETCURSOR) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    if (message == WM_KEYDOWN && (wParam == VK_RETURN || wParam == VK_SPACE)) {
        SendMessageW(GetParent(hwnd), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, helpLinkSubclass, subclassId);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

static std::wstring browseFolder(EditorState& s, const std::wstring& initial, const char* titleKey)
{
    const std::wstring title = tr(s, titleKey);
    BROWSEINFOW info{};
    info.hwndOwner = s.hwnd;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    info.lpszTitle = title.c_str();
    info.lpfn = folderBrowseCallback;
    info.lParam = reinterpret_cast<LPARAM>(initial.c_str());
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (!item) return {};
    wchar_t path[MAX_PATH * 4]{};
    const bool ok = SHGetPathFromIDListW(item, path) != FALSE;
    CoTaskMemFree(item);
    return ok ? fullPath(path) : std::wstring();
}

static bool isDirectory(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void showEditorPage(EditorState& s, int page)
{
    const bool pageChanged = page != s.activePage;
    SendMessageW(s.tab, TCM_SETCURSEL, page, 0);
    s.activePage = page;
    for (HWND form : {s.emptyForm, s.scriptForm, s.htmlForm, s.actionForm, s.executeForm,
                      s.submenuForm, s.separatorForm}) show(form, false);
    show(s.settingsForm, false);
    show(s.header, page == 0);
    if (pageChanged) {
        RECT displayArea{};
        if (s.tab && GetClientRect(s.tab, &displayArea)) {
            SendMessageW(s.tab, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&displayArea));
            MapWindowPoints(s.tab, s.hwnd, reinterpret_cast<POINT*>(&displayArea), 2);
            InvalidateRect(s.hwnd, &displayArea, TRUE);
        }
    }
    if (page != 0) {
        show(s.newButton, false); show(s.runButton, false); show(s.deleteButton, false);
        show(s.upButton, false); show(s.downButton, false); show(s.tree, false);
    }
    if (page == 0) {
        show(s.newButton, true);
        show(s.runButton, true);
        show(s.deleteButton, true);
        show(s.upButton, true);
        show(s.downButton, true);
        show(s.tree, true);
        loadSelected(s, s.selected);
        SetFocus(s.tree);
    } else {
        showChildDialogAndRepaint(s.hwnd, s.settingsForm);
        SetFocus(s.workspaceSet);
    }
}

static void refreshSettingsPaths(EditorState& s)
{
    SetWindowTextW(s.workspacePath, s.workspaceFolder.c_str());
    SetWindowTextW(s.menuJsonPath, s.context.menuPath.c_str());
    SetWindowTextW(s.scriptFolderPath, displayedFolderPath(s.workspaceFolder, s.scriptFolder).c_str());
    SetWindowTextW(s.iconFolderPath, displayedFolderPath(s.workspaceFolder, s.iconFolder).c_str());
}

static void openFolder(EditorState& s, const std::wstring& path)
{
    if (path.empty() || !isDirectory(path)) {
        MessageBoxW(s.hwnd, tr(s, "ConfigFolderMissing").c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    const HINSTANCE result = ShellExecuteW(s.hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
        MessageBoxW(s.hwnd, tr(s, "ConfigFolderOpenFailed").c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING);
}

static bool confirmPendingChanges(EditorState& s)
{
    if (!s.dirty) return true;
    const int answer = MessageBoxW(s.hwnd, tr(s, "ConfigUnsavedPrompt").c_str(),
                                  tr(s, "ConfigTitle").c_str(), MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) return false;
    if (answer == IDYES) {
        std::wstring error;
        if (!saveFile(s, error)) { showSaveError(s, error); return false; }
        if (!saveAndRebuild(s, error)) { showSaveError(s, error); return false; }
    }
    return true;
}

static void onCommand(EditorState& s, int id, int notification)
{
    if (id == ID_SHORTCUT_CLEAR && notification == BN_CLICKED) {
        SetWindowTextW(s.shortcut, L"");
        // The clear button can own focus after a mouse click. Move focus back
        // to the shortcut field before hiding it, or Windows may focus the title edit.
        if (s.shortcut) SetFocus(s.shortcut);
        showShortcutClear(s.shortcut, s.shortcutClear, false);
        s.dirty = true;
        PostMessageW(s.hwnd, TITLE_REFRESH_MESSAGE, 0, 0);
    } else if (id == ID_NEW && notification == BN_CLICKED) {
        HMENU menu = CreatePopupMenu();
        const bool menuRoot = s.selected && s.selected->kind == NodeKind::MenuRoot;
        const bool toolbarRoot = s.selected && s.selected->kind == NodeKind::ToolbarRoot;
        AppendMenuW(menu, MF_STRING | (menuRoot ? MF_GRAYED : 0), ID_NEW_SCRIPT, tr(s, "ConfigScript").c_str());
        AppendMenuW(menu, MF_STRING | (menuRoot ? MF_GRAYED : 0), ID_NEW_HTML, tr(s, "ConfigHtml").c_str());
        AppendMenuW(menu, MF_STRING | (menuRoot ? MF_GRAYED : 0), ID_NEW_EXECUTE, tr(s, "ConfigExecute").c_str());
        AppendMenuW(menu, MF_STRING | (menuRoot ? MF_GRAYED : 0), ID_NEW_ACTION, tr(s, "ConfigAction").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | ((menuRoot || toolbarRoot) ? MF_GRAYED : 0), ID_NEW_SEPARATOR, tr(s, "ConfigSeparator").c_str());
        const bool submenuDisabled = toolbarRoot || (s.selected && s.selected->depth >= 17);
        AppendMenuW(menu, MF_STRING | (submenuDisabled ? MF_GRAYED : 0), ID_NEW_SUBMENU, tr(s, "ConfigSubmenu").c_str());
        RECT rect{}; GetWindowRect(s.newButton, &rect);
        const UINT selection = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, rect.left, rect.bottom, 0, s.hwnd, nullptr);
        DestroyMenu(menu);
        if (selection) addItem(s, static_cast<int>(selection));
    } else if (id == ID_DELETE && notification == BN_CLICKED) deleteSelected(s);
    else if (id == ID_UP && notification == BN_CLICKED) moveSelected(s, true);
    else if (id == ID_DOWN && notification == BN_CLICKED) moveSelected(s, false);
    else if (id == ID_RUN && notification == BN_CLICKED) {
        if (!s.dirty && s.selected && isRunnableNode(s.selected->kind) &&
            s.selected->commandIndex >= 0 && s.context.runSelectedItem)
            s.context.runSelectedItem(static_cast<size_t>(s.selected->commandIndex));
    }
    else if (id == ID_SAVE_REBUILD && notification == BN_CLICKED) {
        clearEditorStatus(s);
        std::wstring error;
        if (!saveFile(s, error)) { MessageBoxW(s.hwnd, error.c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONERROR); return; }
        if (!saveAndRebuild(s, error)) { showSaveError(s, error); return; }
    } else if (id == ID_CANCEL && notification == BN_CLICKED) {
        if (s.dirty) {
            const int answer = MessageBoxW(s.hwnd, tr(s, "ConfigUnsavedPrompt").c_str(), tr(s, "ConfigTitle").c_str(), MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL) return;
            if (answer == IDYES) {
                std::wstring error;
                if (!saveFile(s, error)) { MessageBoxW(s.hwnd, error.c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONERROR); return; }
                if (!saveAndRebuild(s, error)) { showSaveError(s, error); return; }
            }
        }
        EndDialog(s.hwnd, IDCANCEL);
    } else if (!s.updating && id == ID_RUN_IN && notification == CBN_SELCHANGE) s.dirty = true;
    else if (!s.updating && id == ID_CODE_PAGE && (notification == CBN_SELCHANGE || notification == CBN_EDITCHANGE)) s.dirty = true;
    else if (!s.updating && id == ID_SAVE_BEFORE_RUN && notification == BN_CLICKED) s.dirty = true;
    else if (!s.updating && notification == BN_CLICKED && isHideMenuOptionControl(id)) s.dirty = true;
    else if (id == ID_SCRIPT_BROWSE && notification == BN_CLICKED) chooseScriptFile(s);
    else if (id == ID_SCRIPT_EDIT && notification == BN_CLICKED) editScriptFile(s);
    else if (id == ID_ICON && notification == BN_CLICKED) chooseIcon(s);
    else if (id == ID_WORKSPACE_SET && notification == BN_CLICKED) {
        if (!confirmPendingChanges(s)) return;
        if (s.context.chooseWorkspaceFolder && s.context.chooseWorkspaceFolder(s.hwnd, s.pendingStatus))
            EndDialog(s.hwnd, CONFIG_EDITOR_WORKSPACE_CHANGED_RESULT);
    } else if (id == ID_WORKSPACE_OPEN && notification == BN_CLICKED) openFolder(s, s.workspaceFolder);
    else if (id == ID_MENU_JSON_OPEN && notification == BN_CLICKED) {
        if (!fileExists(s.context.menuPath)) {
            MessageBoxW(s.hwnd, tr(s, "MenuJsonMissing").c_str(),
                        tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONERROR);
            return;
        }
        SendMessageW(s.context.owner, NPPM_DOOPEN, 0,
                     reinterpret_cast<LPARAM>(s.context.menuPath.c_str()));
    }
    else if (id == ID_SCRIPT_FOLDER_SET && notification == BN_CLICKED) {
        const std::wstring selected = browseFolder(s, s.scriptFolder, "ConfigSelectScriptFolder");
        if (selected.empty()) return;
        if (!isDirectory(selected)) { MessageBoxW(s.hwnd, tr(s, "ConfigFolderMissing").c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING); return; }
        if (!setString(s.root, "script_folder", storedPath(s.workspaceFolder, selected))) { MessageBoxW(s.hwnd, tr(s, "ConfigSaveFailed").c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONERROR); return; }
        s.scriptFolder = selected;
        s.context.scriptFolder = selected;
        refreshSettingsPaths(s);
        s.dirty = true;
    } else if (id == ID_SCRIPT_FOLDER_OPEN && notification == BN_CLICKED) openFolder(s, s.scriptFolder);
    else if (id == ID_ICON_FOLDER_SET && notification == BN_CLICKED) {
        const std::wstring selected = browseFolder(s, s.iconFolder, "ConfigSelectIconFolder");
        if (selected.empty()) return;
        if (!isDirectory(selected)) { MessageBoxW(s.hwnd, tr(s, "ConfigFolderMissing").c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING); return; }
        if (!setString(s.root, "icon_folder", storedPath(s.workspaceFolder, selected))) { MessageBoxW(s.hwnd, tr(s, "ConfigSaveFailed").c_str(), tr(s, "ConfigTitle").c_str(), MB_OK | MB_ICONERROR); return; }
        s.iconFolder = selected;
        s.context.iconFolder = selected;
        refreshSettingsPaths(s);
        updateIconPreview(s);
        s.dirty = true;
    } else if (id == ID_ICON_FOLDER_OPEN && notification == BN_CLICKED) openFolder(s, s.iconFolder);
    else if (id == ID_SCRIPT && notification == EN_CHANGE && !s.updating) {
        invalidateHtmlAuthorizationIfFolderChanged(s);
        s.dirty = true;
    } else if (id == ID_TITLE && notification == EN_CHANGE && !s.updating) {
        s.dirty = true;
        SetTimer(s.hwnd, TITLE_REFRESH_TIMER, 180, nullptr);
    } else if (id == ID_TITLE && notification == EN_KILLFOCUS && !s.updating) {
        KillTimer(s.hwnd, TITLE_REFRESH_TIMER);
        PostMessageW(s.hwnd, TITLE_REFRESH_MESSAGE, 0, 0);
    } else if (id == ID_ENV_LIST && notification == EN_CHANGE && !s.updating) {
        if (s.selected && s.selected->kind == NodeKind::Execute) {
            s.selected->environmentText = readText(s.envList);
            s.selected->environmentTextLoaded = true;
        }
        s.dirty = true;
    } else if (!s.updating && notification == EN_CHANGE) s.dirty = true;
}

static void addControlFont(EditorState& s, HWND parent)
{
    if (!parent || !s.font) return;
    SendMessageW(parent, WM_SETFONT, reinterpret_cast<WPARAM>(s.font), TRUE);
}

static std::wstring escapeButtonMnemonics(std::wstring text)
{
    size_t position = 0;
    while ((position = text.find(L'&', position)) != std::wstring::npos) {
        text.insert(position, 1, L'&');
        position += 2;
    }
    return text;
}

static bool drawComboItem(const DRAWITEMSTRUCT& draw)
{
    if (draw.CtlType != ODT_COMBOBOX ||
        (draw.CtlID != ID_ACTION && draw.CtlID != ID_RUN_IN && draw.CtlID != ID_CODE_PAGE)) return false;

    const int index = draw.itemID == static_cast<UINT>(-1)
        ? static_cast<int>(SendMessageW(draw.hwndItem, CB_GETCURSEL, 0, 0))
        : static_cast<int>(draw.itemID);
    std::wstring text;
    if (index >= 0) {
        const LRESULT length = SendMessageW(draw.hwndItem, CB_GETLBTEXTLEN, index, 0);
        if (length >= 0) {
            text.resize(static_cast<size_t>(length) + 1);
            const LRESULT copied = SendMessageW(draw.hwndItem, CB_GETLBTEXT, index,
                                                reinterpret_cast<LPARAM>(text.data()));
            if (copied >= 0) text.resize(static_cast<size_t>(copied));
            else text.clear();
        }
    }

    const bool selected = (draw.itemState & ODS_SELECTED) != 0;
    FillRect(draw.hDC, &draw.rcItem,
             GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    RECT textRect = draw.rcItem;
    textRect.left += draw.CtlID == ID_CODE_PAGE ? 3 : 5;
    textRect.right -= 3;
    DrawTextW(draw.hDC, text.c_str(), static_cast<int>(text.size()), &textRect,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    return true;
}

static bool drawShortcutClear(const DRAWITEMSTRUCT& draw, HFONT font, HWND hoveredButton)
{
    if (draw.CtlType != ODT_BUTTON || draw.CtlID != ID_SHORTCUT_CLEAR) return false;

    // Do not fill the owner-drawn button: the shortcut edit below it supplies
    // the background. Use the familiar multiplication-sign close glyph.
    const int saved = SaveDC(draw.hDC);
    SetBkMode(draw.hDC, TRANSPARENT);
    const COLORREF color = (draw.itemState & ODS_DISABLED)
        ? GetSysColor(COLOR_GRAYTEXT)
        : (hoveredButton == draw.hwndItem ? GetSysColor(COLOR_WINDOWTEXT) : RGB(160, 160, 160));
    SetTextColor(draw.hDC, color);
    HGDIOBJ oldFont = font ? SelectObject(draw.hDC, font) : nullptr;
    RECT textRect = draw.rcItem;
    DrawTextW(draw.hDC, L"×", 1, &textRect,
              DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    if (oldFont) SelectObject(draw.hDC, oldFont);
    if (saved) RestoreDC(draw.hDC, saved);
    return true;
}

static bool drawHelpLink(const DRAWITEMSTRUCT& draw)
{
    if (draw.CtlType != ODT_BUTTON ||
        (draw.CtlID != ID_ACTION_HELP && draw.CtlID != ID_COMMAND_HELP && draw.CtlID != ID_ENV_HELP &&
         draw.CtlID != ID_CODE_PAGE_HELP)) return false;
    const int saved = SaveDC(draw.hDC);
    // Owner-draw is the single paint path for this link. Leave the background
    // untouched so the tab page surface shows through behind the text.
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(draw.hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font ? SelectObject(draw.hDC, font) : nullptr;
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, GetSysColor((draw.itemState & ODS_DISABLED) ? COLOR_GRAYTEXT : COLOR_HOTLIGHT));
    RECT textRect = draw.rcItem;
    DrawTextW(draw.hDC, L"(?)", 3, &textRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (oldFont) SelectObject(draw.hDC, oldFont);
    if (saved) RestoreDC(draw.hDC, saved);
    return true;
}

static void positionHelpLink(EditorState& s, HWND label, HWND field, HWND link)
{
    if (!label || !field || !link) return;
    RECT labelRect{}, fieldRect{};
    if (!GetWindowRect(label, &labelRect) || !GetWindowRect(field, &fieldRect)) return;
    HWND form = GetParent(label);
    MapWindowPoints(nullptr, form, reinterpret_cast<POINT*>(&labelRect), 2);
    MapWindowPoints(nullptr, form, reinterpret_cast<POINT*>(&fieldRect), 2);

    const std::wstring text = readText(label);
    HDC dc = GetDC(form);
    SIZE textSize{};
    if (dc) {
        HGDIOBJ oldFont = SelectObject(dc, s.font);
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &textSize);
        if (oldFont) SelectObject(dc, oldFont);
        ReleaseDC(form, dc);
    }
    constexpr int linkWidth = 16;
    constexpr int linkHeight = 12;
    int x = labelRect.left + textSize.cx + 3;
    int y = labelRect.top + ((labelRect.bottom - labelRect.top) - linkHeight) / 2;
    if (x + linkWidth > fieldRect.left - 2) {
        x = labelRect.left;
        y = labelRect.bottom + 1;
    }
    x -= 2;
    SetWindowPos(link, nullptr, x, y, linkWidth, linkHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static INT_PTR CALLBACK pageDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* s = reinterpret_cast<EditorState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        s = reinterpret_cast<EditorState*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (!s) return FALSE;
    switch (message) {
    case WM_CTLCOLORSTATIC:
        {
            HWND control = reinterpret_cast<HWND>(lParam);
            wchar_t className[32]{};
            if (control && GetClassNameW(control, className, _countof(className)) &&
                _wcsicmp(className, L"Edit") == 0)
                return FALSE; // Let the dialog manager paint read-only/disabled edits normally.
        }
        // Static labels should reveal the tab-textured page background instead
        // of painting the default gray button-face rectangle behind their text.
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<INT_PTR>(GetStockObject(NULL_BRUSH));
    case WM_DRAWITEM:
        return SendMessageW(s->hwnd, message, wParam, lParam);
    case WM_COMMAND: case WM_NOTIFY: case WM_MEASUREITEM: case WM_HSCROLL:
        return SendMessageW(s->hwnd, message, wParam, lParam);
    }
    return FALSE;
}

static HWND createPageForm(EditorState& s, int resourceId)
{
    HWND form = CreateDialogParamW(s.context.module, MAKEINTRESOURCEW(resourceId), s.hwnd,
                                  pageDialogProc, reinterpret_cast<LPARAM>(&s));
    if (form) EnableThemeDialogTexture(form, ETDT_ENABLETAB);
    return form;
}

static void layoutPageForm(EditorState& s, HWND form, const RECT& bounds, LONG childReferenceLeft)
{
    if (!form || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;

    struct ChildPlacement { HWND hwnd; RECT dialogRect; };
    std::vector<ChildPlacement> children;
    for (HWND child = GetWindow(form, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT rect{};
        if (!GetWindowRect(child, &rect)) continue;
        MapWindowPoints(nullptr, s.hwnd, reinterpret_cast<POINT*>(&rect), 2);
        children.push_back({child, rect});
    }

    // Place each child dialog in its page rectangle and translate the template
    // controls from their dialog coordinates into the child dialog's client area.
    SetWindowPos(form, HWND_TOP, bounds.left, bounds.top,
                 bounds.right - bounds.left, bounds.bottom - bounds.top,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    for (const ChildPlacement& child : children) {
        const RECT& rect = child.dialogRect;
        SetWindowPos(child.hwnd, nullptr, rect.left - childReferenceLeft, rect.top - bounds.top,
                     rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    // Each page owns its group box so the themed group-box caption is painted
    // over that page's tab-textured background. Size it in client pixels,
    // rather than reusing dialog-template units as pixel coordinates.
    HWND header = GetDlgItem(form, ID_HEADER);
    RECT client{};
    if (header && GetClientRect(form, &client)) {
        SetWindowPos(header, HWND_BOTTOM, 0, 0, client.right, client.bottom,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

static void layoutPageForms(EditorState& s)
{
    RECT tabRect{};
    if (s.tab && GetWindowRect(s.tab, &tabRect))
        MapWindowPoints(nullptr, s.hwnd, reinterpret_cast<POINT*>(&tabRect), 2);
    RECT settingsPage{};
    if (s.tab && GetClientRect(s.tab, &settingsPage)) {
        // The input is the tab control's window client rectangle. Convert it
        // to the page display area so the settings form cannot cover the tabs.
        SendMessageW(s.tab, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&settingsPage));
        MapWindowPoints(s.tab, s.hwnd, reinterpret_cast<POINT*>(&settingsPage), 2);
    } else settingsPage = tabRect;
    RECT header{};
    HWND sampleHeader = GetDlgItem(s.hwnd, ID_HEADER);
    if (sampleHeader && GetWindowRect(sampleHeader, &header))
        MapWindowPoints(nullptr, s.hwnd, reinterpret_cast<POINT*>(&header), 2);
    const LONG itemLeft = header.left ? header.left : 143;
    const LONG itemTop = header.top ? header.top : 38;
    const LONG itemRight = header.right ? header.right : settingsPage.right;
    const LONG itemBottom = header.bottom ? header.bottom : settingsPage.bottom;
    const LONG pageLeft = settingsPage.left ? settingsPage.left : itemLeft;
    const LONG pageTop = settingsPage.top ? settingsPage.top : itemTop;
    const LONG pageRight = settingsPage.right ? settingsPage.right : itemRight;
    const LONG pageBottom = settingsPage.bottom ? settingsPage.bottom : itemBottom;
    RECT itemRegion{std::max(itemLeft, pageLeft), std::max(itemTop, pageTop),
                    std::min(itemRight, pageRight), std::min(itemBottom, pageBottom)};
    for (HWND form : {s.emptyForm, s.scriptForm, s.htmlForm, s.actionForm, s.executeForm,
                      s.submenuForm, s.separatorForm})
        layoutPageForm(s, form, itemRegion, itemRegion.left);
    layoutPageForm(s, s.settingsForm, settingsPage, settingsPage.left);
}

static INT_PTR CALLBACK dialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* s = reinterpret_cast<EditorState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_INITDIALOG) {
        s = reinterpret_cast<EditorState*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        s->hwnd = hwnd;
        s->emptyForm = createPageForm(*s, IDD_ITEM_EMPTY);
        s->scriptForm = createPageForm(*s, IDD_ITEM_SCRIPT);
        s->htmlForm = createPageForm(*s, IDD_ITEM_HTML);
        s->actionForm = createPageForm(*s, IDD_ITEM_ACTION);
        s->executeForm = createPageForm(*s, IDD_ITEM_EXECUTE);
        s->submenuForm = createPageForm(*s, IDD_ITEM_SUBMENU);
        s->separatorForm = createPageForm(*s, IDD_ITEM_SEPARATOR);
        s->settingsForm = createPageForm(*s, IDD_PATHS_SETTINGS);
        s->font = reinterpret_cast<HFONT>(SendMessageW(GetDlgItem(s->scriptForm, ID_TITLE), WM_GETFONT, 0, 0));
        if (!s->font) s->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        LOGFONTW clearFont{};
        if (GetObjectW(s->font, sizeof(clearFont), &clearFont) == sizeof(clearFont)) {
            clearFont.lfHeight = -16;
            clearFont.lfWeight = FW_NORMAL;
            s->shortcutClearFont = CreateFontIndirectW(&clearFont);
        }
        SetWindowTextW(hwnd, tr(*s, "ConfigTitle").c_str());
        s->controlParent = hwnd;
        s->tab = control(*s, WC_TABCONTROLW, L"", WS_TABSTOP, 0, 0, 0, 0, ID_TAB);
        if (s->tab) SetWindowPos(s->tab, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        TCITEMW tabItem{}; tabItem.mask = TCIF_TEXT;
        std::wstring tabText = tr(*s, "ConfigTabMenus"); tabItem.pszText = const_cast<wchar_t*>(tabText.c_str());
        SendMessageW(s->tab, TCM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&tabItem));
        tabText = tr(*s, "ConfigTabSettings"); tabItem.pszText = const_cast<wchar_t*>(tabText.c_str());
        SendMessageW(s->tab, TCM_INSERTITEMW, 1, reinterpret_cast<LPARAM>(&tabItem));
        s->newButton = control(*s, L"BUTTON", tr(*s, "ConfigNew"), 0, 0, 0, 0, 0, ID_NEW);
        s->runButton = control(*s, L"BUTTON", tr(*s, "ConfigRun"), 0, 0, 0, 0, 0, ID_RUN);
        s->deleteButton = control(*s, L"BUTTON", tr(*s, "ConfigDelete"), 0, 0, 0, 0, 0, ID_DELETE);
        s->upButton = control(*s, L"BUTTON", tr(*s, "ConfigMoveUp"), 0, 0, 0, 0, 0, ID_UP);
        s->downButton = control(*s, L"BUTTON", tr(*s, "ConfigMoveDown"), 0, 0, 0, 0, 0, ID_DOWN);
        s->tree = control(*s, WC_TREEVIEWW, L"", 0, 0, 0, 0, 0, ID_TREE);
        initializeTreeImages(*s);

        auto registerItemForm = [&](HWND form, int id, const std::wstring& label = L"") {
            s->controlParent = form;
            HWND itemControl = control(*s, id == ID_SHORTCUT_CLEAR ? L"BUTTON" : L"STATIC",
                                       label, 0, 0, 0, 0, 0, id);
            if (id == ID_SHORTCUT_CLEAR && itemControl)
                SetWindowSubclass(itemControl, shortcutClearHoverSubclass, 2,
                                  reinterpret_cast<DWORD_PTR>(s));
        };
        auto registerHeader = [&](HWND form, const char* heading) {
            registerItemForm(form, ID_HEADER, tr(*s, heading));
        };
        HWND layoutHeader = GetDlgItem(hwnd, ID_HEADER);
        if (layoutHeader) ShowWindow(layoutHeader, SW_HIDE);
        for (HWND form : {s->emptyForm, s->scriptForm, s->htmlForm, s->actionForm, s->executeForm,
                          s->submenuForm, s->separatorForm})
            registerHeader(form, "ConfigItemSettings");
        registerItemForm(s->emptyForm, ID_INFO);
        registerItemForm(s->separatorForm, ID_INFO);
        for (HWND form : {s->scriptForm, s->htmlForm, s->submenuForm, s->actionForm, s->executeForm}) {
            registerItemForm(form, ID_TITLE_LABEL, tr(*s, "ConfigTitleLabel"));
            registerItemForm(form, ID_TITLE);
            if (form != s->submenuForm) {
                registerItemForm(form, ID_SHORTCUT_LABEL, tr(*s, "ConfigShortcut"));
                registerItemForm(form, ID_SHORTCUT);
                registerItemForm(form, ID_SHORTCUT_CLEAR, L"×");
            }
            if (form == s->scriptForm || form == s->htmlForm) {
                registerItemForm(form, ID_SCRIPT_LABEL, tr(*s, form == s->htmlForm ? "ConfigHtmlFolder" : "ConfigScriptFile"));
                registerItemForm(form, ID_SCRIPT);
                registerItemForm(form, ID_SCRIPT_BROWSE, tr(*s, "ConfigBrowse"));
                registerItemForm(form, ID_SCRIPT_EDIT, tr(*s, "ConfigEditScript"));
                if (form == s->htmlForm)
                {
                    s->htmlHelpText = GetDlgItem(form, ID_HTML_HELP_RICH_EDIT);
                    s->htmlAdvancedEnable = GetDlgItem(form, ID_HTML_ADVANCED_ENABLE);
                    s->htmlAuthModeGroup = GetDlgItem(form, ID_HTML_AUTH_MODE_GROUP);
                    s->htmlAuthReconfirm = GetDlgItem(form, ID_HTML_AUTH_RECONFIRM);
                    s->htmlAuthTrustFolder = GetDlgItem(form, ID_HTML_AUTH_TRUST_FOLDER);
                    SetWindowTextW(s->htmlAdvancedEnable, tr(*s, "ConfigHtmlAdvancedPermissions").c_str());
                    SetWindowTextW(s->htmlAuthModeGroup, tr(*s, "ConfigHtmlAuthorizationMode").c_str());
                    SetWindowTextW(s->htmlAuthReconfirm, tr(*s, "ConfigHtmlReconfirm").c_str());
                    SetWindowTextW(s->htmlAuthTrustFolder, tr(*s, "ConfigHtmlTrustFolder").c_str());
                }
                else
                    s->scriptHelpText = GetDlgItem(form, ID_SCRIPT_HELP_RICH_EDIT);
            } else if (form == s->actionForm) {
                registerItemForm(form, ID_ACTION_LABEL, tr(*s, "ConfigAction"));
                registerItemForm(form, ID_ACTION);
                registerItemForm(form, ID_ACTION_TARGET_LABEL, tr(*s, "ConfigMenuPathOrId"));
                registerItemForm(form, ID_ACTION_TARGET);
                registerItemForm(form, ID_ACTION_HELP);
                s->actionHelpText = GetDlgItem(form, ID_ACTION_HELP_RICH_EDIT);
            } else if (form == s->executeForm) {
                registerItemForm(form, ID_COMMAND_LABEL, tr(*s, "ConfigCommand"));
                registerItemForm(form, ID_COMMAND);
                registerItemForm(form, ID_COMMAND_HELP);
                registerItemForm(form, ID_SAVE_BEFORE_RUN, tr(*s, "ConfigSaveBeforeRun"));
                registerItemForm(form, ID_RUN_IN_LABEL, tr(*s, "ConfigRunIn"));
                registerItemForm(form, ID_RUN_IN);
                registerItemForm(form, ID_CODE_PAGE_LABEL, tr(*s, "ConfigCodePage"));
                registerItemForm(form, ID_CODE_PAGE_HELP);
                registerItemForm(form, ID_CODE_PAGE);
                registerItemForm(form, ID_ENV_LABEL, tr(*s, "ConfigEnvironment"));
                registerItemForm(form, ID_ENV_LIST);
                registerItemForm(form, ID_ENV_HELP);
            } else if (form == s->submenuForm) {
                s->submenuHelpText = GetDlgItem(form, ID_SUBMENU_HELP_RICH_EDIT);
            }
        }
        s->shortcutHintText = tr(*s, "ConfigShortcutHint");
        s->shortcutTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                              WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                              hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        for (HWND form : {s->scriptForm, s->htmlForm, s->actionForm, s->executeForm, s->submenuForm}) {
            HWND shortcut = GetDlgItem(form, ID_SHORTCUT);
            if (shortcut && s->shortcutTooltip) {
                TOOLINFOW tool{};
                tool.cbSize = sizeof(tool);
                tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                tool.hwnd = form;
                tool.uId = reinterpret_cast<UINT_PTR>(shortcut);
                tool.lpszText = const_cast<LPWSTR>(s->shortcutHintText.c_str());
                SendMessageW(s->shortcutTooltip, TTM_ADDTOOLW, 0,
                             reinterpret_cast<LPARAM>(&tool));
            }
            show(GetDlgItem(form, ID_SHORTCUT_CLEAR), false);
        }
        const std::wstring envPlaceholder = tr(*s, "ConfigEnvironmentPlaceholder");
        SendMessageW(GetDlgItem(s->executeForm, ID_ENV_LIST), EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(envPlaceholder.c_str()));

        s->controlParent = s->settingsForm;
        s->workspacePath = control(*s, L"EDIT", L"", 0, 0, 0, 0, 0, ID_WORKSPACE_PATH);
        control(*s, L"STATIC", tr(*s, "ConfigWorkspaceFolder"), 0, 0, 0, 0, 0, ID_WORKSPACE_LABEL);
        s->workspaceSet = control(*s, L"BUTTON", tr(*s, "ConfigSetFolder"), 0, 0, 0, 0, 0, ID_WORKSPACE_SET);
        s->workspaceOpen = control(*s, L"BUTTON", tr(*s, "ConfigOpenFolder"), 0, 0, 0, 0, 0, ID_WORKSPACE_OPEN);
        s->menuJsonPath = control(*s, L"EDIT", L"", 0, 0, 0, 0, 0, ID_MENU_JSON_PATH);
        control(*s, L"STATIC", tr(*s, "ConfigMenuJsonPath"), 0, 0, 0, 0, 0, ID_MENU_JSON_LABEL);
        s->menuJsonBrowse = control(*s, L"BUTTON", tr(*s, "ConfigSetFolder"), 0, 0, 0, 0, 0, ID_MENU_JSON_BROWSE);
        s->menuJsonOpen = control(*s, L"BUTTON", tr(*s, "ConfigOpenFolder"), 0, 0, 0, 0, 0, ID_MENU_JSON_OPEN);
        EnableWindow(s->menuJsonBrowse, FALSE);
        s->scriptFolderPath = control(*s, L"EDIT", L"", 0, 0, 0, 0, 0, ID_SCRIPT_FOLDER_PATH);
        control(*s, L"STATIC", tr(*s, "ConfigScriptFolder"), 0, 0, 0, 0, 0, ID_SCRIPT_FOLDER_LABEL);
        s->scriptFolderSet = control(*s, L"BUTTON", tr(*s, "ConfigSetFolder"), 0, 0, 0, 0, 0, ID_SCRIPT_FOLDER_SET);
        s->scriptFolderOpen = control(*s, L"BUTTON", tr(*s, "ConfigOpenFolder"), 0, 0, 0, 0, 0, ID_SCRIPT_FOLDER_OPEN);
        s->iconFolderPath = control(*s, L"EDIT", L"", 0, 0, 0, 0, 0, ID_ICON_FOLDER_PATH);
        control(*s, L"STATIC", tr(*s, "ConfigIconFolder"), 0, 0, 0, 0, 0, ID_ICON_FOLDER_LABEL);
        s->iconFolderSet = control(*s, L"BUTTON", tr(*s, "ConfigSetFolder"), 0, 0, 0, 0, 0, ID_ICON_FOLDER_SET);
        s->iconFolderOpen = control(*s, L"BUTTON", tr(*s, "ConfigOpenFolder"), 0, 0, 0, 0, 0, ID_ICON_FOLDER_OPEN);
        s->pathsInfoGroup = control(*s, L"BUTTON", tr(*s, "ConfigPathsGroup"), 0, 0, 0, 0, 0, ID_PATHS_INFO_GROUP);
        s->pathsInfoRichEdit = GetDlgItem(s->settingsForm, ID_PATHS_INFO_RICH_EDIT);
        s->hideMenuGroup = control(*s, L"BUTTON", tr(*s, "ConfigHideMenusGroup"), 0, 0, 0, 0, 0, ID_HIDE_MENU_GROUP);
        s->hideEncoding = control(*s, L"BUTTON", tr(*s, "ConfigHideEncoding"), 0, 0, 0, 0, 0, ID_HIDE_ENCODING);
        s->hideLanguage = control(*s, L"BUTTON", tr(*s, "ConfigHideLanguage"), 0, 0, 0, 0, 0, ID_HIDE_LANGUAGE);
        s->hideTools = control(*s, L"BUTTON", tr(*s, "ConfigHideTools"), 0, 0, 0, 0, 0, ID_HIDE_TOOLS);
        s->hideMacro = control(*s, L"BUTTON", tr(*s, "ConfigHideMacro"), 0, 0, 0, 0, 0, ID_HIDE_MACRO);
        s->hideRun = control(*s, L"BUTTON", tr(*s, "ConfigHideRun"), 0, 0, 0, 0, 0, ID_HIDE_RUN);
        s->hideMenuRestart = control(*s, L"STATIC", tr(*s, "ConfigHideMenusRestart"), 0, 0, 0, 0, 0, ID_HIDE_MENU_RESTART);
        loadHideMenuOptions(*s);
        s->controlParent = hwnd;
        s->saveButton = control(*s, L"BUTTON", L"", 0, 0, 0, 0, 0, ID_SAVE_REBUILD);
        if (s->saveButton) SetWindowTextW(s->saveButton, escapeButtonMnemonics(tr(*s, "ConfigSaveRebuild")).c_str());
        // Let the common-control status bar calculate its own height and dock
        // to the parent bottom, following the documented Win32 creation pattern.
        s->statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                                       WS_CHILD | WS_VISIBLE | CCS_BOTTOM,
                                       0, 0, 0, 0, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDITOR_STATUS)),
                                       s->context.module, nullptr);
        if (s->statusBar) {
            SendMessageW(s->statusBar, WM_SETFONT, reinterpret_cast<WPARAM>(s->font), FALSE);
            SendMessageW(s->statusBar, SB_SIMPLE, FALSE, 0);
            int statusPart[] = {-1};
            SendMessageW(s->statusBar, SB_SETPARTS, 1, reinterpret_cast<LPARAM>(statusPart));
        }
        s->workspaceFolder = s->context.workspaceFolder;
        refreshSettingsPaths(*s);

        // Keep horizontal text margins consistent across standard EDIT fields.
        for (HWND form : {s->scriptForm, s->htmlForm, s->actionForm, s->executeForm, s->submenuForm}) {
            setEditTextMargins(GetDlgItem(form, ID_TITLE), 0, 0);
            setEditTextMargins(GetDlgItem(form, ID_SHORTCUT), 0, 0);
        }
        setEditTextMargins(GetDlgItem(s->scriptForm, ID_SCRIPT), 0, 0);
        setEditTextMargins(GetDlgItem(s->htmlForm, ID_SCRIPT), 0, 0);
        setEditTextMargins(GetDlgItem(s->actionForm, ID_ACTION_TARGET), 0, 0);
        for (int id : {ID_WORKSPACE_PATH, ID_MENU_JSON_PATH, ID_SCRIPT_FOLDER_PATH, ID_ICON_FOLDER_PATH})
            setEditTextMargins(GetDlgItem(s->settingsForm, id), 0, 0);
        COMBOBOXINFO codePageCombo{sizeof(codePageCombo)};
        if (GetComboBoxInfo(GetDlgItem(s->executeForm, ID_CODE_PAGE), &codePageCombo) && codePageCombo.hwndItem) {
            const LONG_PTR editStyle = GetWindowLongPtrW(codePageCombo.hwndItem, GWL_STYLE);
            SetWindowLongPtrW(codePageCombo.hwndItem, GWL_STYLE, editStyle | ES_MULTILINE);
            setEditTextMargins(codePageCombo.hwndItem, 0, 0);
            setMultilineTextOrigin(codePageCombo.hwndItem, 2, 1);
        }
        for (HWND form : {s->scriptForm, s->htmlForm, s->submenuForm, s->actionForm, s->executeForm}) {
            setMultilineTextOrigin(GetDlgItem(form, ID_TITLE), 2, 1);
            setMultilineTextOrigin(GetDlgItem(form, ID_SHORTCUT), 2, 1);
        }
        setMultilineTextOrigin(GetDlgItem(s->scriptForm, ID_SCRIPT), 2, 1);
        setMultilineTextOrigin(GetDlgItem(s->htmlForm, ID_SCRIPT), 2, 1);
        setMultilineTextOrigin(GetDlgItem(s->actionForm, ID_ACTION_TARGET), 2, 1);
        for (int id : {ID_WORKSPACE_PATH, ID_MENU_JSON_PATH, ID_SCRIPT_FOLDER_PATH, ID_ICON_FOLDER_PATH})
            setMultilineTextOrigin(GetDlgItem(s->settingsForm, id), 2, 1);
        setMultilineTextOrigin(GetDlgItem(s->executeForm, ID_COMMAND), 2, 1);
        setMultilineTextOrigin(GetDlgItem(s->executeForm, ID_ENV_LIST), 2, 1);

        s->controlParent = s->actionForm;
        s->action = GetDlgItem(s->actionForm, ID_ACTION);
        fillActionCombo(*s, L"RUN_CURRENT_FILE");
        const wchar_t* codePages[] = {L"ANSI", L"UTF-8", L"UTF-16LE", L"UTF-16BE", L"UTF-32LE", L"UTF-32BE", L"US-ASCII", L"ISO-8859-1", L"UTF-7"};
        s->codePage = GetDlgItem(s->executeForm, ID_CODE_PAGE);
        for (const wchar_t* value : codePages) SendMessageW(s->codePage, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        s->runIn = GetDlgItem(s->executeForm, ID_RUN_IN);
        fillRunIn(*s, L"output_pane");
        s->controlParent = hwnd;
        layoutPageForms(*s);
        setRichHelpContent(*s, s->scriptHelpText,
            {{tr(*s, "ConfigScriptHelpRuntime"), s->context.scriptRequirementsUrl},
             {tr(*s, "ConfigScriptHelpCreate"), s->context.scriptTemplateUrl},
             {tr(*s, "ConfigScriptHelpApi"), s->context.scriptApiUrl}}, 100);
        setRichHelpContent(*s, s->htmlHelpText,
            {{tr(*s, "ConfigHtmlHelp"), L""},
             {tr(*s, "ConfigHtmlPermissionsHelp"), s->context.htmlPermissionsUrl}}, 100);
        setRichHelpContent(*s, s->actionHelpText,
            {{tr(*s, "ConfigActionHelpHeading"), L""},
             {tr(*s, "ConfigActionHelpCurrentFile"), L""},
             {tr(*s, "ConfigActionHelpStop"), L""},
             {tr(*s, "ConfigActionHelpMenuCommand"), L""},
             {tr(*s, "ConfigActionHelpConfigure"), L""}}, 80, 1);
        setRichHelpContent(*s, s->submenuHelpText,
            {{tr(*s, "ConfigSubmenuHelp"), L""}}, 0);
        positionHelpLink(*s, GetDlgItem(s->actionForm, ID_ACTION_TARGET_LABEL),
                         GetDlgItem(s->actionForm, ID_ACTION_TARGET),
                         GetDlgItem(s->actionForm, ID_ACTION_HELP));
        const std::wstring pathsInfo = tr(*s, "ConfigPathsInfoText");
        std::vector<std::pair<std::wstring, std::wstring>> pathParagraphs;
        size_t i = 0;
        while (i < pathsInfo.size()) {
            while (i < pathsInfo.size() && (pathsInfo[i] == L'\r' || pathsInfo[i] == L'\n')) ++i;
            const size_t paragraphStart = i;
            while (i < pathsInfo.size() && pathsInfo[i] != L'\r' && pathsInfo[i] != L'\n') ++i;
            if (i > paragraphStart)
                pathParagraphs.emplace_back(pathsInfo.substr(paragraphStart, i - paragraphStart), L"");
        }
        setRichHelpContent(*s, s->pathsInfoRichEdit, pathParagraphs, 100);
        if (s->pathsInfoRichEdit) {
            // The path group's sibling controls are created around this rich edit.
            // Keep the text surface above the group-box frame and repaint it once
            // after its initial content has been assigned.
            SetWindowPos(s->pathsInfoRichEdit, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            InvalidateRect(s->pathsInfoRichEdit, nullptr, TRUE);
            UpdateWindow(s->pathsInfoRichEdit);
        }
        bindItemForm(*s, nullptr);
        s->workspaceFolder = s->context.workspaceFolder;
        refreshSettingsPaths(*s);
        s->controlParent = s->executeForm;
        s->commandLabel = GetDlgItem(s->executeForm, ID_COMMAND_LABEL);
        s->command = GetDlgItem(s->executeForm, ID_COMMAND);
        s->commandHelp = GetDlgItem(s->executeForm, ID_COMMAND_HELP);
        s->codePageLabel = GetDlgItem(s->executeForm, ID_CODE_PAGE_LABEL);
        s->codePage = GetDlgItem(s->executeForm, ID_CODE_PAGE);
        s->codePageHelp = GetDlgItem(s->executeForm, ID_CODE_PAGE_HELP);
        s->envLabel = GetDlgItem(s->executeForm, ID_ENV_LABEL);
        s->envList = GetDlgItem(s->executeForm, ID_ENV_LIST);
        s->envHelp = GetDlgItem(s->executeForm, ID_ENV_HELP);
        positionHelpLink(*s, s->commandLabel, s->command, s->commandHelp);
        positionHelpLink(*s, s->codePageLabel, s->codePage, s->codePageHelp);
        positionHelpLink(*s, s->envLabel, s->envList, s->envHelp);
        s->controlParent = hwnd;
        showEditorPage(*s, 0);
        RECT dialogRect{}, ownerRect{};
        if (GetWindowRect(hwnd, &dialogRect) && s->context.owner &&
            GetWindowRect(s->context.owner, &ownerRect)) {
            const int dialogWidth = dialogRect.right - dialogRect.left;
            const int dialogHeight = dialogRect.bottom - dialogRect.top;
            const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - dialogWidth) / 2;
            const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - dialogHeight) / 2;
            SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ShowWindow(hwnd, SW_SHOW);
        SetFocus(s->tree);
        rebuildTree(*s);
        // Set the opening message after the initial tree has finished building,
        // because building it can emit selection notifications.
        if (s->statusBar && !s->context.initialStatus.empty()) {
            s->statusText = s->context.initialStatus;
            SendMessageW(s->statusBar, SB_SETTEXTW, SBT_OWNERDRAW, 0);
            UpdateWindow(s->statusBar);
        }
        s->dirty = false;
        return FALSE;
    }
    if (!s) return FALSE;
    if (message == TITLE_REFRESH_MESSAGE) {
        if (s->selected && commitSelected(*s)) refreshSelectedTreeItemTitle(*s);
        updateRunButton(*s);
        return TRUE;
    }
    if (message == WM_DRAWITEM &&
        drawShortcutClear(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam), s->shortcutClearFont,
                          s->shortcutClearHovered)) return TRUE;
    if (message == WM_DRAWITEM && drawEditorStatus(*s, *reinterpret_cast<DRAWITEMSTRUCT*>(lParam))) return TRUE;
    if (message == WM_DRAWITEM &&
        drawHelpLink(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam))) return TRUE;
    if (message == WM_DRAWITEM &&
        drawComboItem(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam))) return TRUE;
    if (message == WM_DRAWITEM && wParam == ID_ICON) {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw && draw->CtlType == ODT_BUTTON) {
            FillRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_BTNFACE));
            DrawEdge(draw->hDC, &draw->rcItem,
                     (draw->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
            if (s->iconPreview) {
                const int width = draw->rcItem.right - draw->rcItem.left;
                const int height = draw->rcItem.bottom - draw->rcItem.top;
                const int x = draw->rcItem.left + (width - 32) / 2;
                const int y = draw->rcItem.top + (height - 32) / 2;
                DrawIconEx(draw->hDC, x, y, s->iconPreview, 32, 32, 0, nullptr, DI_NORMAL);
            }
            if (draw->itemState & ODS_FOCUS) {
                RECT focus = draw->rcItem;
                InflateRect(&focus, -4, -4);
                DrawFocusRect(draw->hDC, &focus);
            }
            return TRUE;
        }
    }
    if (message == WM_NOTIFY) {
        const auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->code == EN_LINK) {
            const auto* linkEvent = reinterpret_cast<const ENLINK*>(lParam);
            const bool activated = linkEvent->msg == WM_LBUTTONUP ||
                (linkEvent->msg == WM_KEYUP && linkEvent->wParam == VK_RETURN);
            if (activated) {
                for (const RichHelpLink& link : s->richHelpLinks) {
                    if (link.control == header->hwndFrom && linkEvent->chrg.cpMin < link.end &&
                        linkEvent->chrg.cpMax > link.start && !link.url.empty()) {
                        ShellExecuteW(hwnd, L"open", link.url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        return TRUE;
                    }
                }
            }
        }
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == ID_TAB && reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE) {
        clearEditorStatus(*s);
        const int page = static_cast<int>(SendMessageW(s->tab, TCM_GETCURSEL, 0, 0));
        if (s->activePage == 0 && page != 0 && !commitSelected(*s)) {
            SendMessageW(s->tab, TCM_SETCURSEL, s->activePage, 0);
            MessageBoxW(hwnd, tr(*s, "ConfigInvalidJson").c_str(),
                        tr(*s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        showEditorPage(*s, page);
        return TRUE;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == ID_TREE &&
        reinterpret_cast<NMHDR*>(lParam)->code == NM_RCLICK) {
        POINT screen{};
        if (GetCursorPos(&screen)) showTreeContextMenu(*s, screen);
        return TRUE;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == ID_TREE &&
        reinterpret_cast<NMHDR*>(lParam)->code == TVN_ITEMEXPANDEDW) {
        const auto* expanded = reinterpret_cast<const NMTREEVIEWW*>(lParam);
        EditorNode* node = reinterpret_cast<EditorNode*>(expanded->itemNew.lParam);
        if (node && (node->kind == NodeKind::TopMenu || node->kind == NodeKind::Submenu)) {
            const int image = (expanded->action == TVE_EXPAND) ? 7 : 0;
            TVITEMW update{};
            update.mask = TVIF_HANDLE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            update.hItem = expanded->itemNew.hItem;
            update.iImage = image;
            update.iSelectedImage = image;
            TreeView_SetItem(s->tree, &update);
        }
        return TRUE;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == ID_TREE &&
        reinterpret_cast<NMHDR*>(lParam)->code == TVN_BEGINDRAGW) {
        auto* drag = reinterpret_cast<NMTREEVIEWW*>(lParam);
        beginTreeDrag(*s, drag->itemNew.hItem);
        return TRUE;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom == ID_TREE && reinterpret_cast<NMHDR*>(lParam)->code == TVN_SELCHANGEDW) {
        if (s->rebuildingTree) return TRUE;
        clearEditorStatus(*s);
        auto* change = reinterpret_cast<NMTREEVIEWW*>(lParam);
        auto* node = reinterpret_cast<EditorNode*>(change->itemNew.lParam);
        if (!commitSelected(*s)) { MessageBoxW(hwnd, tr(*s, "ConfigDuplicateMenuTitle").c_str(), tr(*s, "ConfigTitle").c_str(), MB_OK | MB_ICONWARNING); return TRUE; }
        loadSelected(*s, node);
        return TRUE;
    }
    if (message == WM_MOUSEMOVE && s->draggingTreeItem) {
        updateTreeDrag(*s);
        return TRUE;
    }
    if (message == WM_LBUTTONUP && s->draggingTreeItem) {
        endTreeDrag(*s, true);
        return TRUE;
    }
    if (message == WM_CAPTURECHANGED && s->draggingTreeItem) {
        s->draggingTreeItem = false;
        clearTreeDropFeedback(*s);
        s->dragItem = nullptr;
        SetCursor(systemCursor(32512)); // IDC_ARROW
        return TRUE;
    }
    if (message == WM_CANCELMODE && s->draggingTreeItem) {
        endTreeDrag(*s, false);
        return TRUE;
    }
    if (message == WM_TIMER && wParam == TITLE_REFRESH_TIMER) {
        KillTimer(hwnd, TITLE_REFRESH_TIMER);
        PostMessageW(hwnd, TITLE_REFRESH_MESSAGE, 0, 0);
        return TRUE;
    }
    if (message == WM_TIMER && wParam == TREE_DRAG_EXPAND_TIMER) {
        KillTimer(hwnd, TREE_DRAG_EXPAND_TIMER);
        const HTREEITEM expandItem = s->pendingExpand;
        s->pendingExpand = nullptr;
        if (s->draggingTreeItem && expandItem && s->dragTarget == expandItem &&
            s->dropPosition == DropPosition::Into) {
            TreeView_Expand(s->tree, expandItem, TVE_EXPAND);
            TreeView_SelectDropTarget(s->tree, expandItem);
        }
        return TRUE;
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam), notification = HIWORD(wParam);
        const bool isUserAction = notification == BN_CLICKED || notification == EN_CHANGE ||
                                  notification == CBN_SELCHANGE || notification == CBN_EDITCHANGE;
        if (!s->updating && isUserAction && id != ID_SAVE_REBUILD)
            clearEditorStatus(*s);
        if (id == ID_ACTION_HELP && notification == BN_CLICKED) {
            const std::wstring url = nativeLanguageXmlUrl(s->context.owner);
            ShellExecuteW(hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if ((id == ID_COMMAND_HELP || id == ID_ENV_HELP) && notification == BN_CLICKED) {
            if (!s->context.placeholdersUrl.empty())
                ShellExecuteW(hwnd, L"open", s->context.placeholdersUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if (id == ID_CODE_PAGE_HELP && notification == BN_CLICKED) {
            ShellExecuteW(hwnd, L"open",
                          L"https://learn.microsoft.com/windows/win32/intl/code-page-identifiers",
                          nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        if (id == ID_HTML_ADVANCED_ENABLE && notification == BN_CLICKED) {
            const bool requested =
                IsDlgButtonChecked(s->htmlForm, ID_HTML_ADVANCED_ENABLE) == BST_CHECKED;
            if (requested) {
                const std::wstring folder = canonicalHtmlFolderPath(
                    resolvedPath(s->workspaceFolder, readText(s->script)));
                std::wstring currentHash;
                const bool hasIndex = !folder.empty() && htmlFolderHasIndex(folder);
                const INT_PTR result = hasIndex ? showHtmlPermissionPreview(*s) : IDCANCEL;
                const bool acceptedForPreview = result == IDOK && hasIndex;
                if (acceptedForPreview) {
                    s->htmlAuthorizationConfirmed = true;
                    s->htmlAuthorizedFolder = folder;
                    if (IsDlgButtonChecked(s->htmlForm, ID_HTML_AUTH_TRUST_FOLDER) != BST_CHECKED &&
                        !htmlFolderSha256(folder, currentHash)) {
                        s->htmlAuthorizationConfirmed = false;
                        s->htmlAuthorizedFolder.clear();
                        currentHash.clear();
                        CheckDlgButton(s->htmlForm, ID_HTML_ADVANCED_ENABLE, BST_UNCHECKED);
                        MessageBoxW(hwnd, tr(*s, "ConfigHtmlIndexMissing").c_str(),
                                    L"NppMenuScripts", MB_OK | MB_ICONWARNING);
                    }
                    s->htmlAuthorizedSha256 = currentHash;
                } else {
                    s->htmlAuthorizationConfirmed = false;
                    s->htmlAuthorizedFolder.clear();
                    s->htmlAuthorizedSha256.clear();
                    if (!hasIndex)
                        MessageBoxW(hwnd, tr(*s, "ConfigHtmlIndexMissing").c_str(),
                                    L"NppMenuScripts", MB_OK | MB_ICONWARNING);
                }
                CheckDlgButton(s->htmlForm, ID_HTML_ADVANCED_ENABLE,
                               acceptedForPreview && s->htmlAuthorizationConfirmed ? BST_CHECKED : BST_UNCHECKED);
                updateHtmlAuthorizationOptions(*s, acceptedForPreview && s->htmlAuthorizationConfirmed);
            } else {
                s->htmlAuthorizationConfirmed = false;
                s->htmlAuthorizedFolder.clear();
                s->htmlAuthorizedSha256.clear();
                updateHtmlAuthorizationOptions(*s, false);
            }
            s->dirty = true;
            updateRunButton(*s);
            return TRUE;
        }
        if ((id == ID_HTML_AUTH_RECONFIRM || id == ID_HTML_AUTH_TRUST_FOLDER) &&
            notification == BN_CLICKED) {
            s->dirty = true;
            updateRunButton(*s);
            return TRUE;
        }
        if (id == ID_ACTION && notification == CBN_SELCHANGE) {
            const int kind = static_cast<int>(SendMessageW(s->action, CB_GETITEMDATA, SendMessageW(s->action, CB_GETCURSEL, 0, 0), 0));
            showActionTargetControls(*s, kind == 3); s->dirty = true; updateRunButton(*s); return TRUE;
        }
        onCommand(*s, id, notification);
        updateRunButton(*s);
        return TRUE;
    }
    if (message == WM_CLOSE) { onCommand(*s, ID_CANCEL, BN_CLICKED); return TRUE; }
    if (message == WM_DESTROY) {
        savePersistentTreeExpansion(*s);
        if (s->shortcutTooltip) { DestroyWindow(s->shortcutTooltip); s->shortcutTooltip = nullptr; }
        if (s->shortcutClearFont) { DeleteObject(s->shortcutClearFont); s->shortcutClearFont = nullptr; }
        if (s->customIcon) { DestroyIcon(s->customIcon); s->customIcon = nullptr; }
        if (s->treeImages) {
            TreeView_SetImageList(s->tree, nullptr, TVSIL_NORMAL);
            ImageList_Destroy(s->treeImages);
            s->treeImages = nullptr;
        }
        return TRUE;
    }
    return FALSE;
}

static cJSON* loadJson(const std::wstring& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) return nullptr;
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) bytes.erase(0, 3);
    return bytes.empty() ? nullptr : cJSON_Parse(bytes.c_str());
}

} // namespace

MenuConfigEditorResult showMenuConfigEditor(const MenuConfigEditorContext& context,
                                            std::wstring* statusAfterWorkspaceChange)
{
    EditorState state;
    state.context = context;
    loadPersistentTreeExpansion(state);
    state.iconFolder = context.iconFolder;
    state.scriptFolder = context.scriptFolder;
    state.root = loadJson(context.menuPath);
    if (!cJSON_IsObject(state.root)) {
        if (state.root) cJSON_Delete(state.root);
        MessageBoxW(context.owner, context.translate ? context.translate("ConfigInvalidJson").c_str() : L"menu.json could not be opened.",
                    context.translate ? context.translate("ConfigTitle").c_str() : L"NppMenuScripts", MB_OK | MB_ICONERROR);
        return MenuConfigEditorResult::Closed;
    }
    state.menu = member(state.root, "menu");
    state.toolbar = member(state.root, "toolbar");
    if (!cJSON_IsObject(state.menu)) {
        cJSON_Delete(state.root);
        MessageBoxW(context.owner, context.translate ? context.translate("ConfigInvalidJson").c_str() : L"menu.json is invalid.",
                    context.translate ? context.translate("ConfigTitle").c_str() : L"NppMenuScripts", MB_OK | MB_ICONERROR);
        return MenuConfigEditorResult::Closed;
    }
    for (cJSON* group = state.menu->child; group; group = group->next) {
        if (!cJSON_IsArray(group)) {
            cJSON_Delete(state.root);
            MessageBoxW(context.owner, context.translate ? context.translate("ConfigInvalidJson").c_str() : L"menu.json is invalid.",
                        context.translate ? context.translate("ConfigTitle").c_str() : L"NppMenuScripts", MB_OK | MB_ICONERROR);
            return MenuConfigEditorResult::Closed;
        }
    }

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        cJSON_Delete(state.root);
        MessageBoxW(context.owner, L"Unable to initialize the menu configuration controls.", L"NppMenuScripts", MB_OK | MB_ICONERROR);
        return MenuConfigEditorResult::Closed;
    }
    HMODULE richEditModule = LoadLibraryW(L"Msftedit.dll");
    if (!richEditModule) {
        cJSON_Delete(state.root);
        MessageBoxW(context.owner, L"Unable to load the Windows Rich Edit control.",
                    L"NppMenuScripts", MB_OK | MB_ICONERROR);
        return MenuConfigEditorResult::Closed;
    }
    const int result = DialogBoxParamW(context.module, MAKEINTRESOURCEW(IDD_MENU_CONFIG), context.owner,
                                       dialogProc, reinterpret_cast<LPARAM>(&state));
    FreeLibrary(richEditModule);
    if (statusAfterWorkspaceChange && result == CONFIG_EDITOR_WORKSPACE_CHANGED_RESULT)
        *statusAfterWorkspaceChange = state.pendingStatus;
    cJSON_Delete(state.root);
    if (result == CONFIG_EDITOR_WORKSPACE_CHANGED_RESULT) return MenuConfigEditorResult::WorkspaceChanged;
    return MenuConfigEditorResult::Closed;
}

