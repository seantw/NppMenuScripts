#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <memory>
#include <cctype>
#include <functional>
#include <cstring>
#include <climits>
#include <limits>
#include <cwchar>
#include <utility>
#include <unordered_map>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>

#ifndef NPPMENUSCRIPTS_DEBUG_DEFAULT
#define NPPMENUSCRIPTS_DEBUG_DEFAULT 0
#endif

#include "PluginInterface.h"
#include "Notepad_plus_msgs.h"
#include "Docking.h"
#include "version.h"
#include "ScriptExitState.h"
#include "MenuCommand.h"
extern "C" {
#include "../third_party/cJSON.h"
}

static const wchar_t PLUGIN_NAME[] = L"NppMenuScripts";
// Keep this synchronized with menu-helper.js. Change it only when helper API
// compatibility changes, not for routine implementation fixes.
static constexpr unsigned int HELPER_API_ID = 12;
static constexpr char HELPER_API_CHECK_PREFIX[] = "\x1eNPPMENUSCRIPTS_HELPER_API:";
static NppData g_nppData{};
static HMODULE g_hModule = nullptr;

static void showAbout();
static void openScriptsFolder();
static void openMenuJson();
static void rebuildMenu();
static void setWorkspaceFolder();
static void stopScript();
static bool installMenus(const std::wstring& jsonPath, bool forceRebuild = false);
static void removeMenus();
static void runToolbarScript(size_t index);
static bool setNppStatusText(const std::wstring& text);
static void loadTranslations();
static const std::wstring& tr(const char* key);
static void applyTranslatedPluginMenu();
static LRESULT CALLBACK pluginMenuMessageHook(int code, WPARAM wParam, LPARAM lParam);
static void installPluginMenuMessageHook();
static void removePluginMenuMessageHook();
static std::unordered_map<std::string, std::wstring> g_translations;
static void debugLog(const std::wstring& message);
static std::wstring joinPath(std::wstring base, std::wstring relative);
static bool postOutputMessage(const std::wstring& text, bool isStderr = false);
enum class ExecuteRunIn { OutputPane, Console, Windows };
struct ExecuteOptions {
    std::wstring command;
    bool saveBeforeRun = false;
    UINT codePage = GetACP();
    ExecuteRunIn runIn = ExecuteRunIn::OutputPane;
    std::unordered_map<std::wstring, std::wstring> environment;

    bool operator==(const ExecuteOptions& other) const
    {
        return command == other.command && saveBeforeRun == other.saveBeforeRun &&
            codePage == other.codePage && runIn == other.runIn && environment == other.environment;
    }
};
static FuncItem g_funcItems[] = {
    { L"Open Scripts Folder", openScriptsFolder, 0, false, nullptr },
    { L"Open menu.json", openMenuJson, 0, false, nullptr },
    { L"Rebuild Menu && Toolbar", rebuildMenu, 0, false, nullptr },
    { L"Set Workspace Folder...", setWorkspaceFolder, 0, false, nullptr },
    { L"Stop Script", stopScript, 0, false, nullptr },
    { L"", nullptr, 0, false, nullptr },
    { L"", showAbout, 0, false, nullptr }
};
static std::vector<FuncItem> g_allFuncItems;
static std::vector<std::wstring> g_toolbarScriptFiles;
static std::vector<MenuCommandAction> g_toolbarActions;
static std::vector<std::wstring> g_toolbarActionTargets;
static std::vector<ExecuteOptions> g_toolbarExecuteOptions;
static std::vector<std::wstring> g_toolbarIconFiles;
static std::vector<std::wstring> g_toolbarTitles;
static std::vector<toolbarIconsWithDarkMode> g_toolbarIcons;
static bool g_toolbarIconsRegistered = false;
static int g_menuScriptCount = 0;

static HMENU g_mainMenu = nullptr;
static std::vector<HMENU> g_topMenus;
static int g_cmdBase = 0;
static int g_cmdCount = 0;
static std::vector<int> g_cmdIds;
static std::vector<std::wstring> g_scriptPaths;
static std::vector<std::wstring> g_scriptTitles;
static std::vector<MenuCommandAction> g_scriptActions;
static std::vector<ExecuteOptions> g_executeOptions;
static bool g_installed = false;
static HWND g_hotkeyWnd = nullptr;
static const wchar_t HOTKEY_CLASS_NAME[] = L"NppMenuScripts_HotkeyWindow";

static HWND g_outputPane = nullptr;
static HWND g_outputEdit = nullptr;
static COLORREF g_outputFore = RGB(0, 0, 0);
static COLORREF g_outputBack = RGB(255, 255, 255);
static int g_outputCaretStyle = 1;
static bool g_outputCaretStyleCaptured = false;
static int g_outputTechnology = 0;
static int g_outputBorderWidth = 0;
static const wchar_t OUTPUT_CLASS_NAME[] = L"NppMenuScripts_OutputPane";
static std::wstring g_outputTitle = L"Output Pane";
static std::wstring g_outputDefaultTitle = L"Output Pane";
static int g_stdoutAnsiColor = -1;
static int g_stderrAnsiColor = -1;
static std::wstring g_scriptFolder;
static std::wstring g_workspaceFolder;
static std::wstring g_debugLogPath;
static bool g_debugEnabled = (NPPMENUSCRIPTS_DEBUG_DEFAULT != 0);
static std::wstring g_menuJsonPath;
static std::wstring g_iconFolder;
static const int OUTPUT_DLG_ID = 0x4E4A;
static DockedWidgetData g_outputDock{};
static bool g_outputShownForRun = false;
static std::mutex g_processMutex;
static std::thread g_processThread;
static HANDLE g_stdinWrite = nullptr;
static std::mutex g_stdinMutex;
static std::atomic<bool> g_processRunning{false};
static std::atomic<bool> g_scriptSetStatusBarText{false};
static std::atomic<bool> g_scriptSetCursor{false};
static std::atomic<bool> g_shuttingDown{false};
static std::atomic<bool> g_helperApiVerified{false};
static std::atomic<bool> g_helperApiMismatchReported{false};
static std::mutex g_processHandleMutex;
static HANDLE g_processHandle = nullptr;
static HANDLE g_processJob = nullptr;
static std::atomic<bool> g_stopRequested{false};
static constexpr UINT WM_NPPMENUSCRIPTS_OUTPUT = WM_APP + 101;
static constexpr UINT WM_NPPMENUSCRIPTS_RUNSTATE = WM_APP + 102;
static constexpr UINT WM_NPPMENUSCRIPTS_PROTOCOL = WM_APP + 103;
static constexpr UINT WM_NPPMENUSCRIPTS_REFRESH_LANGUAGE = WM_APP + 104;
static constexpr UINT_PTR NPPMENUSCRIPTS_LANGUAGE_TIMER = 0x4E4C;
static unsigned int g_languageRefreshPasses = 0;
static HHOOK g_pluginMenuMessageHook = nullptr;

struct ProtocolDispatchRequest {
    std::string line;
    bool handled = false;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
};

struct RunStatePayload {
    std::wstring text;
    bool restoreEditorFocus = false;
    bool showStopNotification = false;
    bool updateOutputCaret = true;
};

static LRESULT CALLBACK hotkeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static bool createHotkeyWindow();
static void destroyHotkeyWindow();
static LRESULT CALLBACK outputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static bool createOutputPane();
static void destroyOutputPane();
static void showOutputPane();
static HWND currentScintilla();
static bool hasScriptExtension(const std::wstring& path);
static std::wstring scriptExtension(const std::wstring& path);

static constexpr UINT NPPM_GETFULLPATHFROMBUFFERID_LOCAL = NPPMSG + 58;
static constexpr UINT NPPM_GETPOSFROMBUFFERID_LOCAL = NPPMSG + 57;
static constexpr UINT NPPM_GETCURRENTBUFFERID_LOCAL = NPPMSG + 60;
static constexpr UINT NPPM_ACTIVATEDOC_LOCAL = NPPMSG + 28;
static constexpr UINT NPPM_GETCURRENTVIEW_LOCAL = NPPMSG + 88;

static constexpr UINT SCI_GETCURRENTPOS_LOCAL = 2008;
static constexpr UINT SCI_GETFIRSTVISIBLELINE_LOCAL = 2152;
static constexpr UINT SCI_DOCLINEFROMVISIBLE_LOCAL = 2221;
static constexpr UINT SCI_GETLINEVISIBLE_LOCAL = 2228;
static constexpr UINT SCI_ENSUREVISIBLE_LOCAL = 2232;
static constexpr UINT SCI_GOTOPOS_LOCAL = 2025;
static constexpr UINT SCI_LINEFROMPOSITION_LOCAL = 2166;
static constexpr UINT SCI_GETCOLUMN_LOCAL = 2129;
static constexpr UINT SCI_GETLINECOUNT_LOCAL = 2154;
static constexpr UINT SCI_GETLINE_LOCAL = 2153;
static constexpr UINT SCI_GETSELTEXT_LOCAL = 2161;
static constexpr UINT SCI_REPLACESEL_LOCAL = 2170;
static constexpr UINT SCI_GETTEXT_LOCAL = 2182;
static constexpr UINT SCI_GETTEXTLENGTH_LOCAL = 2183;
static constexpr UINT SCI_POSITIONFROMLINE_LOCAL = 2167;
static constexpr UINT SCI_FINDCOLUMN_LOCAL = 2456;
static constexpr UINT SCI_VISIBLEFROMDOCLINE_LOCAL = 2220;
static constexpr UINT SCI_SETFIRSTVISIBLELINE_LOCAL = 2613;
static constexpr UINT SCI_SETXOFFSET_LOCAL = 2397;
static constexpr UINT SCI_GETXOFFSET_LOCAL = 2398;
static constexpr UINT SCI_GETLINEENDPOSITION_LOCAL = 2136;
static constexpr UINT SCI_LINELENGTH_LOCAL = 2350;
static constexpr UINT SCI_GETSELECTIONSTART_LOCAL = 2143;
static constexpr UINT SCI_GETSELECTIONEND_LOCAL = 2145;
static constexpr UINT SCI_SETSEL_LOCAL = 2160;
static constexpr UINT SCI_GETMODIFY_LOCAL = 2159;
static constexpr UINT SCI_GETCODEPAGE_LOCAL = 2137;

static std::wstring utf8ToWide(const char* s)
{
    if (!s) return L"";
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0);
    if (n <= 0) n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

static std::wstring scintillaBytesToWide(HWND sci, const std::string& bytes)
{
    if (bytes.empty()) return L"";
    const UINT codePage = static_cast<UINT>(SendMessageW(sci, SCI_GETCODEPAGE_LOCAL, 0, 0));
    if (codePage == 65001) return utf8ToWide(bytes.c_str());

    // Scintilla reports code page 0 for documents using the system ANSI code page.
    const UINT effectiveCodePage = codePage == 0 ? CP_ACP : codePage;
    const int needed = MultiByteToWideChar(effectiveCodePage, 0, bytes.data(),
                                           static_cast<int>(bytes.size()), nullptr, 0);
    if (needed <= 0) return L"";
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(effectiveCodePage, 0, bytes.data(), static_cast<int>(bytes.size()),
                        result.data(), needed);
    return result;
}

static std::wstring utf8ChunkToWide(const std::string& bytes)
{
    if (bytes.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (n <= 0) n = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), out.data(), n);
    return out;
}

static std::wstring moduleDirectory()
{
    wchar_t path[MAX_PATH * 4] = {};
    DWORD n = GetModuleFileNameW(g_hModule, path, static_cast<DWORD>(std::size(path)));
    if (!n) return L".";
    std::wstring s(path, n);
    const size_t p = s.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"." : s.substr(0, p);
}

static const std::wstring& tr(const char* key)
{
    static const std::wstring empty;
    static const std::unordered_map<std::string, std::wstring> builtInEnglish = {
        {"OutputPane", L"Output Pane"}, {"OpenScriptsFolder", L"Open Scripts Folder"},
        {"OpenMenuJson", L"Open menu.json"}, {"RebuildMenuToolbar", L"Rebuild Menu && Toolbar"},
        {"SetWorkspaceFolder", L"Set Workspace Folder..."},
        {"About", L"About NppMenuScripts (v{version})"},
        {"Toolbar", L"Toolbar"}, {"ScriptRunning", L"Running - {name}"},
        {"ScriptCompleted", L"Script completed"}, {"ScriptFailed", L"Script failed"},
        {"ExternalCommandCompleted", L"External command completed"},
        {"ExternalCommandStarted", L"External command started"},
        {"ExternalCommandFailed", L"External command failed"},
        {"ExternalCommandStopped", L"External command stopped"},
        {"MenuRebuilt", L"Rebuilt: {path}"},
        {"PleaseWaitSetWorkspacePath", L"Please wait until the current script has finished before changing the workspace folder."},
        {"PleaseWait", L"Please wait until the current script has finished."},
        {"PleaseWaitRebuild", L"Please wait until the current script has finished before rebuilding the menu."},
        {"OpenScriptsFolderFailed", L"Unable to open the scripts folder."},
        {"ScriptsFolderMissing", L"The scripts folder does not exist. Create it?"},
        {"CreateScriptsFolderFailed", L"Unable to create the scripts folder."},
        {"MenuJsonMissing", L"The current menu.json file does not exist."},
        {"WorkspaceInitFailed", L"Unable to initialize the workspace. It must be empty or contain menu.json."},
        {"WorkspacePathSaveFailed", L"Unable to save the workspace path."},
        {"WorkspaceInsidePluginFolder", L"Choose a workspace outside the NppMenuScripts plugin folder so Plugin Admin updates cannot replace your files."},
        {"SelectWorkspaceFolder", L"Select NppMenuScripts workspace folder"},
        {"WorkspacePathSet", L"Workspace: {path}"},
        {"OpenGithubFailed", L"Unable to open the NppMenuScripts GitHub page."},
        {"ScriptAlreadyRunning", L"A script is already running."},
        {"HelperIncompatible", L"The NppMenuScripts helper module is incompatible with the loaded NppMenuScripts.dll.\r\n\r\nCopy the matching helper module next to NppMenuScripts.dll or into the parent directory of script_folder, then run the script again."},
        {"CreateScriptPrompt", L"{name} does not exist. Create it?"},
        {"CreateScriptFailed", L"Unable to create the script file."},
        {"CurrentFileUnnamed", L"Save the current file before running it."},
        {"CurrentFileUnsupported", L"Unsupported current file type: {extension}. Supported types are .js, .mjs, .pl, and .py."},
        {"CurrentFileModified", L"The current file has unsaved changes. Save it before running?"},
        {"CurrentFileSaveFailed", L"The current file could not be saved. The script was not run."},
        {"MenuCommandFailed", L"The requested menu command could not be found or is unavailable."},
        {"MenuCommandSelfInvoke", L"This toolbar action points to an NppMenuScripts command and would invoke itself. The action was not run."},
        {"EnvironmentValueTooLong", L"One or more environment variable values exceed the Windows limit. Truncate them and run the command?"},
        {"EnvironmentBlockTooLarge", L"The command environment is too large for Windows. Reduce its environment variables before running it."},
        {"UnableStartCommand", L"[NppMenuScripts] Unable to start the command. Error code: {error}\r\n"},
        {"MenuJsonEmpty", L"menu.json was not found or is empty."},
        {"JsonParseError", L"JSON parse error: {error}"},
        {"InvalidMenuJson", L"menu.json must contain string 'script_folder' and object 'menu'. Menu items must be {title, script}, {title, action}, {title, execute}, {title, sub}, or an empty object for a separator. Use exactly one of 'script', 'action', or 'execute'; supported actions are 'RUN_CURRENT_FILE', 'STOP_SCRIPT', or 'MENU:<command ID or menu path>'."},
        {"InvalidToolbarEntry", L"Each 'toolbar' entry needs string 'title' and exactly one of 'script', 'action', or 'execute'. 'icon' is optional. Scripts must use .js, .mjs, .pl, or .py; supported actions are 'RUN_CURRENT_FILE', 'STOP_SCRIPT', or 'MENU:<command ID or menu path>'."},
        {"InvalidToolbarIcon", L"Toolbar icons must use the .ico format."},
        {"ToolbarLimit", L"A maximum of 32 toolbar buttons is supported."},
        {"AllocateMenuIdsFailed", L"Notepad++ could not allocate menu command IDs."},
        {"HotkeyWindowFailed", L"Cannot create hotkey window."},
        {"MainMenuFailed", L"Cannot get Notepad++ main menu."},
        {"UnableCreatePipes", L"[NppMenuScripts] Unable to create stdout/stderr pipes.\r\n"},
        {"UnableStartNode", L"[NppMenuScripts] Unable to start node.exe. Error code: {error}\r\n"},
        {"UnableStartPerl", L"[NppMenuScripts] Unable to start perl.exe. Error code: {error}\r\n"},
        {"UnableStartPython", L"[NppMenuScripts] Unable to start py.exe. Error code: {error}\r\n"},
    };
    const auto it = g_translations.find(key);
    if (it != g_translations.end()) return it->second;
    const auto fallback = builtInEnglish.find(key);
    return fallback == builtInEnglish.end() ? empty : fallback->second;
}

static std::wstring trFormat(const char* key, const std::vector<std::pair<std::wstring, std::wstring>>& values)
{
    std::wstring result = tr(key);
    for (const auto& value : values) {
        size_t at = 0;
        while ((at = result.find(value.first, at)) != std::wstring::npos) {
            result.replace(at, value.first.size(), value.second);
            at += value.second.size();
        }
    }
    return result;
}

static std::wstring nativeLocaleCode()
{
    const LRESULT needed = SendMessageW(g_nppData._nppHandle, NPPM_GETNATIVELANGFILENAME, 0, 0);
    if (needed <= 0 || needed > 1024) {
        debugLog(L"NPPM_GETNATIVELANGFILENAME returned length=" + std::to_wstring(needed));
        return L"";
    }
    std::vector<char> filename(static_cast<size_t>(needed) + 1, '\0');
    const LRESULT copied = SendMessageW(g_nppData._nppHandle, NPPM_GETNATIVELANGFILENAME,
                                        static_cast<WPARAM>(filename.size()),
                                        reinterpret_cast<LPARAM>(filename.data()));
    if (copied <= 0) {
        debugLog(L"NPPM_GETNATIVELANGFILENAME copy returned length=" + std::to_wstring(copied));
        return L"";
    }
    debugLog(L"Native language filename='" + utf8ToWide(filename.data()) + L"'");
    std::string name(filename.data(), static_cast<size_t>(copied));
    const size_t separator = name.find_last_of("/\\");
    if (separator != std::string::npos) name = name.substr(separator + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name.resize(dot);
    if (name == "taiwanesemandarin" || name == "chinese") return L"zh-TW";
    if (name == "chinesesimplified") return L"zh-CN";
    if (name == "japanese") return L"ja";
    if (name == "french") return L"fr";
    if (name == "spanish") return L"es";
    if (name == "portuguese") return L"pt-PT";
    if (name == "brazilian_portuguese" || name == "brazilianportuguese") return L"pt-BR";
    return L"en";
}

static void loadTranslations()
{
    const std::wstring path = joinPath(moduleDirectory(), L"i18n.json");
    std::ifstream file(path.c_str(), std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) bytes.erase(0, 3);
    cJSON* root = bytes.empty() ? nullptr : cJSON_Parse(bytes.c_str());
    if (!cJSON_IsObject(root)) {
        debugLog(L"i18n.json missing or invalid; using built-in English strings");
        if (root) cJSON_Delete(root);
        return;
    }
    const std::wstring locale = nativeLocaleCode();
    if (locale.empty() && !g_translations.empty()) {
        debugLog(L"Native UI locale is temporarily unavailable; retaining current translations");
        cJSON_Delete(root);
        return;
    }
    g_translations.clear();
    const std::wstring effectiveLocale = locale.empty() ? L"en" : locale;
    const std::string localeUtf8 = effectiveLocale == L"zh-TW" ? "zh-TW" : effectiveLocale == L"zh-CN" ? "zh-CN" :
                                   effectiveLocale == L"pt-PT" ? "pt-PT" : effectiveLocale == L"pt-BR" ? "pt-BR" :
                                   effectiveLocale == L"ja" ? "ja" : effectiveLocale == L"fr" ? "fr" : effectiveLocale == L"es" ? "es" : "en";
    cJSON* english = cJSON_GetObjectItemCaseSensitive(root, "en");
    cJSON* selected = cJSON_GetObjectItemCaseSensitive(root, localeUtf8.c_str());
    for (cJSON* item = english ? english->child : nullptr; item; item = item->next) {
        if (item->string && cJSON_IsString(item) && item->valuestring[0]) g_translations[item->string] = utf8ToWide(item->valuestring);
    }
    if (selected && selected != english) {
        for (cJSON* item = selected->child; item; item = item->next) {
            if (item->string && cJSON_IsString(item) && item->valuestring[0]) g_translations[item->string] = utf8ToWide(item->valuestring);
        }
    }
    debugLog(L"Native UI locale=" + effectiveLocale + L" translation entries=" + std::to_wstring(g_translations.size()));
    cJSON_Delete(root);
}

static bool setMenuLabelByCommand(HMENU menu, UINT commandId, const wchar_t* label)
{
    if (!menu || !label) return false;
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    info.dwTypeData = const_cast<wchar_t*>(label);
    return SetMenuItemInfoW(menu, commandId, FALSE, &info) != FALSE;
}

static void applyTranslatedPluginMenu()
{
    const char* keys[] = { "OpenScriptsFolder", "OpenMenuJson", "RebuildMenuToolbar", "SetWorkspaceFolder", "StopScript", "", "About" };
    for (size_t i = 0; i < std::size(keys); ++i) {
        if (!keys[i][0]) continue;
        std::wstring label = tr(keys[i]);
        if (i == 6) {
            const std::wstring version = std::to_wstring(NPP_MENUSCRIPTS_VERSION_MAJOR) + L"." +
                std::to_wstring(NPP_MENUSCRIPTS_VERSION_MINOR) + L"." + std::to_wstring(NPP_MENUSCRIPTS_VERSION_PATCH) + L"." +
                std::to_wstring(NPP_MENUSCRIPTS_VERSION_BUILD);
            label = trFormat("About", {{L"{version}", version}});
        }
        if (label.empty()) continue;
        wcsncpy_s(g_funcItems[i]._itemName, label.c_str(), _TRUNCATE);
        if (i < g_allFuncItems.size()) wcsncpy_s(g_allFuncItems[i]._itemName, label.c_str(), _TRUNCATE);
    }

    HMENU pluginsMenu = reinterpret_cast<HMENU>(SendMessageW(g_nppData._nppHandle, NPPM_GETMENUHANDLE, NPPPLUGINMENU, 0));
    HMENU pluginMenu = nullptr;
    for (int i = 0; pluginsMenu && i < GetMenuItemCount(pluginsMenu); ++i) {
        wchar_t label[128] = {};
        GetMenuStringW(pluginsMenu, static_cast<UINT>(i), label, static_cast<int>(std::size(label)), MF_BYPOSITION);
        std::wstring normalized = label;
        normalized.erase(std::remove(normalized.begin(), normalized.end(), L'&'), normalized.end());
        if (normalized == PLUGIN_NAME) {
            MENUITEMINFOW info{}; info.cbSize = sizeof(info); info.fMask = MIIM_SUBMENU;
            if (GetMenuItemInfoW(pluginsMenu, static_cast<UINT>(i), TRUE, &info)) pluginMenu = info.hSubMenu;
            break;
        }
    }
    if (!pluginMenu) {
        debugLog(L"Translated plugin menu not found for '" + std::wstring(PLUGIN_NAME) + L"'");
        return;
    }
    for (size_t i = 0; i < std::size(g_funcItems) && i < g_allFuncItems.size(); ++i) {
        const bool updated = setMenuLabelByCommand(pluginMenu, static_cast<UINT>(g_allFuncItems[i]._cmdID), g_funcItems[i]._itemName);
        debugLog(L"Translated plugin menu command id=" + std::to_wstring(g_allFuncItems[i]._cmdID) +
                 L" label='" + g_funcItems[i]._itemName + L"' updated=" + std::to_wstring(updated));
    }
    if (g_allFuncItems.size() > std::size(g_funcItems)) {
        const std::wstring toolbarLabel = tr("Toolbar");
        for (int i = 0; i < GetMenuItemCount(pluginMenu); ++i) {
            MENUITEMINFOW info{}; info.cbSize = sizeof(info); info.fMask = MIIM_SUBMENU | MIIM_STRING;
            wchar_t label[128] = {}; info.dwTypeData = label; info.cch = static_cast<UINT>(std::size(label));
            if (GetMenuItemInfoW(pluginMenu, static_cast<UINT>(i), TRUE, &info) && info.hSubMenu) {
                const bool isToolbar = std::wstring(label) == L"Toolbar" || (!toolbarLabel.empty() && std::wstring(label) == toolbarLabel);
                if (isToolbar && !toolbarLabel.empty()) {
                    info.fMask = MIIM_STRING; info.dwTypeData = const_cast<wchar_t*>(toolbarLabel.c_str());
                    SetMenuItemInfoW(pluginMenu, static_cast<UINT>(i), TRUE, &info);
                    break;
                }
            }
        }
    }
    DrawMenuBar(g_nppData._nppHandle);
}

static LRESULT CALLBACK pluginMenuMessageHook(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0 && lParam) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message->message == WM_INITMENU || message->message == WM_INITMENUPOPUP) {
            const HMENU pluginsMenu = reinterpret_cast<HMENU>(
                SendMessageW(g_nppData._nppHandle, NPPM_GETMENUHANDLE, NPPPLUGINMENU, 0));
            if (reinterpret_cast<HMENU>(message->wParam) == pluginsMenu) {
                debugLog(L"Plugins menu opened; reapplying translated labels");
                applyTranslatedPluginMenu();
            }
        }
    }
    return CallNextHookEx(g_pluginMenuMessageHook, code, wParam, lParam);
}

static void installPluginMenuMessageHook()
{
    if (g_pluginMenuMessageHook || !g_nppData._nppHandle) return;
    const DWORD threadId = GetWindowThreadProcessId(g_nppData._nppHandle, nullptr);
    if (!threadId) return;
    g_pluginMenuMessageHook = SetWindowsHookExW(WH_CALLWNDPROC, pluginMenuMessageHook, g_hModule, threadId);
    debugLog(L"Plugins menu message hook installed=" + std::to_wstring(g_pluginMenuMessageHook != nullptr));
}

static void removePluginMenuMessageHook()
{
    if (!g_pluginMenuMessageHook) return;
    UnhookWindowsHookEx(g_pluginMenuMessageHook);
    g_pluginMenuMessageHook = nullptr;
    debugLog(L"Plugins menu message hook removed");
}

static std::string debugWideToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

static void debugLog(const std::wstring& message)
{
    if (!g_debugEnabled || g_debugLogPath.empty()) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::ofstream f(g_debugLogPath.c_str(), std::ios::binary | std::ios::app);
    if (!f) return;
    char prefix[128] = {};
    sprintf_s(prefix, sizeof(prefix), "%04u-%02u-%02u %02u:%02u:%02u.%03u T%lu ",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
              st.wMilliseconds, static_cast<unsigned long>(GetCurrentThreadId()));
    f.write(prefix, static_cast<std::streamsize>(strlen(prefix)));
    const std::string utf8 = debugWideToUtf8(message);
    f.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    f.write("\r\n", 2);
    f.flush();
}

static std::wstring hexPtr(const void* p)
{
    wchar_t buf[32] = {};
    swprintf_s(buf, L"0x%p", p);
    return buf;
}

static std::wstring readUtf8File(const std::wstring& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return L"";
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF)
        bytes.erase(0, 3);
    return utf8ToWide(bytes.c_str());
}

static std::wstring joinPath(std::wstring base, std::wstring name)
{
    std::replace(base.begin(), base.end(), L'/', L'\\');
    std::replace(name.begin(), name.end(), L'/', L'\\');
    if (base.empty()) return name;
    if (base.back() == L'\\') return base + name;
    return base + L"\\" + name;
}

static std::wstring directoryName(const std::wstring& path)
{
    const size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos) return L".";
    if (p == 0) return path.substr(0, 1);
    return path.substr(0, p);
}

static bool isAbsolutePath(const std::wstring& path)
{
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') return true;
    if (path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
        path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) return true;
    return false;
}

static std::wstring normalizeWindowsPathSeparators(std::wstring path)
{
    std::replace(path.begin(), path.end(), L'/', L'\\');
    return path;
}

static std::wstring fullPath(const std::wstring& path)
{
    if (path.empty()) return L"";
    wchar_t buffer[MAX_PATH * 4] = {};
    DWORD n = GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(buffer)), buffer, nullptr);
    if (!n || n >= std::size(buffer)) return path;
    return std::wstring(buffer, n);
}

static std::wstring resolveScriptFolder(const std::wstring& menuPath, const std::wstring& configuredFolder)
{
    if (isAbsolutePath(configuredFolder)) return fullPath(configuredFolder);
    return fullPath(joinPath(directoryName(menuPath), configuredFolder));
}

static std::wstring notepadPluginConfigDirectory()
{
    const LRESULT needed = SendMessageW(g_nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, 0, 0);
    if (needed > 0 && needed < 32768) {
        std::vector<wchar_t> path(static_cast<size_t>(needed) + 1, L'\0');
        const LRESULT copied = SendMessageW(g_nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR,
                                            static_cast<WPARAM>(path.size()), reinterpret_cast<LPARAM>(path.data()));
        if (copied && path[0]) return fullPath(path.data());
    }

    // NPPM_GETPLUGINSCONFIGDIR is available in supported Notepad++ versions.
    // Keep a per-user fallback in case an older host does not answer the message.
    wchar_t appData[MAX_PATH * 4] = {};
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, static_cast<DWORD>(std::size(appData)));
    if (!n || n >= std::size(appData)) return L"";
    return fullPath(joinPath(joinPath(joinPath(std::wstring(appData, n), L"Notepad++"), L"plugins"), L"Config"));
}

static std::wstring defaultWorkspaceDirectory()
{
    const std::wstring config = notepadPluginConfigDirectory();
    if (config.empty()) return L"";
    return fullPath(joinPath(joinPath(config, L"NppMenuScripts"), L"workspace"));
}

static bool isPathWithinDirectory(const std::wstring& directory, const std::wstring& path)
{
    std::wstring parent = fullPath(directory);
    std::wstring child = fullPath(path);
    if (parent.empty() || child.empty()) return false;
    if (_wcsicmp(parent.c_str(), child.c_str()) == 0) return true;
    if (!parent.empty() && parent.back() != L'\\') parent.push_back(L'\\');
    return child.size() >= parent.size() && _wcsnicmp(child.c_str(), parent.c_str(), parent.size()) == 0;
}

static bool ensureDirectoryExists(const std::wstring& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const int result = SHCreateDirectoryExW(g_nppData._nppHandle, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_FILE_EXISTS || result == ERROR_ALREADY_EXISTS;
}

static bool copyFileIfMissing(const std::wstring& source, const std::wstring& destination)
{
    const DWORD attrs = GetFileAttributesW(destination.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    if (CopyFileW(source.c_str(), destination.c_str(), TRUE)) return true;
    return GetLastError() == ERROR_FILE_EXISTS;
}

static bool copyDirectoryMissing(const std::wstring& source, const std::wstring& destination)
{
    const DWORD sourceAttrs = GetFileAttributesW(source.c_str());
    if (sourceAttrs == INVALID_FILE_ATTRIBUTES || !(sourceAttrs & FILE_ATTRIBUTE_DIRECTORY)) return true;
    if (!ensureDirectoryExists(destination)) return false;

    WIN32_FIND_DATAW data{};
    const std::wstring pattern = joinPath(source, L"*");
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;

    bool success = true;
    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0 ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            _wcsicmp(data.cFileName, L"NppMenuScripts-debug.log") == 0) continue;
        const std::wstring src = joinPath(source, data.cFileName);
        const std::wstring dst = joinPath(destination, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!copyDirectoryMissing(src, dst)) success = false;
        } else if (!copyFileIfMissing(src, dst)) {
            success = false;
        }
    } while (success && FindNextFileW(find, &data));
    FindClose(find);
    return success;
}

static bool directoryIsEmpty(const std::wstring& path)
{
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(joinPath(path, L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return false;

    bool empty = true;
    do {
        if (wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0) {
            empty = false;
            break;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return empty;
}

static std::wstring readMenuStringProperty(const std::wstring& path, const char* name, const std::wstring& fallback)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) bytes.erase(0, 3);
    cJSON* root = bytes.empty() ? nullptr : cJSON_Parse(bytes.c_str());
    cJSON* value = root ? cJSON_GetObjectItemCaseSensitive(root, name) : nullptr;
    const std::wstring result = cJSON_IsString(value) ? utf8ToWide(value->valuestring) : fallback;
    if (root) cJSON_Delete(root);
    return result;
}

static bool copyConfiguredFolderFromTemplate(const std::wstring& configuredPath,
                                            const std::wstring& workspaceDirectory,
                                            const std::wstring& templateDirectory)
{
    const std::wstring source = isAbsolutePath(configuredPath)
        ? fullPath(configuredPath) : fullPath(joinPath(templateDirectory, configuredPath));
    const std::wstring templateRoot = fullPath(templateDirectory);
    if (source.empty() || _wcsicmp(source.c_str(), templateRoot.c_str()) == 0 ||
        !isPathWithinDirectory(templateRoot, source)) return true;
    std::wstring relative = source.substr(templateRoot.size());
    while (!relative.empty() && (relative.front() == L'\\' || relative.front() == L'/')) relative.erase(relative.begin());
    if (relative.empty()) return true;
    return copyDirectoryMissing(source, joinPath(workspaceDirectory, relative));
}

static bool initializeWorkspaceContents(const std::wstring& workspaceDirectory)
{
    if (workspaceDirectory.empty() || isPathWithinDirectory(moduleDirectory(), workspaceDirectory) ||
        !ensureDirectoryExists(workspaceDirectory)) return false;

    const std::wstring pluginDirectory = moduleDirectory();
    const std::wstring templateDirectory = joinPath(pluginDirectory, L"workspace-template");
    const std::wstring sourceMenu = joinPath(templateDirectory, L"menu.json");
    const std::wstring workspaceMenu = joinPath(workspaceDirectory, L"menu.json");
    const bool initializeDefaults = directoryIsEmpty(workspaceDirectory);
    if (initializeDefaults && !copyFileIfMissing(sourceMenu, workspaceMenu)) return false;
    if (!initializeDefaults && GetFileAttributesW(workspaceMenu.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    // The JS package import map and its target must travel with scripts in the workspace.
    for (const wchar_t* name : { L"package.json", L"menu-helper.js" }) {
        const std::wstring source = joinPath(pluginDirectory, name);
        const std::wstring destination = joinPath(workspaceDirectory, name);
        if (GetFileAttributesW(source.c_str()) != INVALID_FILE_ATTRIBUTES && !CopyFileW(source.c_str(), destination.c_str(), FALSE))
            return false;
    }

    if (!initializeDefaults) return true;

    const std::wstring scriptFolder = readMenuStringProperty(sourceMenu, "script_folder", L"./script");
    const std::wstring iconFolder = readMenuStringProperty(sourceMenu, "icon_folder", L"./icon");
    return copyConfiguredFolderFromTemplate(scriptFolder, workspaceDirectory, templateDirectory) &&
           copyConfiguredFolderFromTemplate(iconFolder, workspaceDirectory, templateDirectory);
}

static std::wstring settingsFilePath()
{
    wchar_t appData[MAX_PATH * 4] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, static_cast<DWORD>(std::size(appData)));
    if (!n || n >= std::size(appData)) return L"";
    return joinPath(joinPath(joinPath(std::wstring(appData, n), L"Notepad++"), L"plugins"), L"config\\NppMenuScripts.ini");
}

static std::wstring loadConfiguredWorkspacePath()
{
    const std::wstring ini = settingsFilePath();
    if (!ini.empty()) {
        wchar_t value[MAX_PATH * 4] = {};
        const DWORD n = GetPrivateProfileStringW(L"Settings", L"workspace_dir", L"", value,
                                                 static_cast<DWORD>(std::size(value)), ini.c_str());
        if (n > 0) return fullPath(value);
    }
    return defaultWorkspaceDirectory();
}

static bool saveConfiguredWorkspacePath(const std::wstring& path)
{
    const std::wstring ini = settingsFilePath();
    if (ini.empty()) return false;
    const std::wstring configDir = directoryName(ini);
    CreateDirectoryW(directoryName(configDir).c_str(), nullptr);
    CreateDirectoryW(configDir.c_str(), nullptr);
    return WritePrivateProfileStringW(L"Settings", L"workspace_dir", path.c_str(), ini.c_str()) != FALSE;
}

static std::wstring workspaceMenuPath()
{
    const std::wstring workspace = g_workspaceFolder.empty() ? loadConfiguredWorkspacePath() : g_workspaceFolder;
    return workspace.empty() ? L"" : joinPath(workspace, L"menu.json");
}

static std::wstring trim(const std::wstring& s)
{
    const size_t first = s.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return L"";
    const size_t last = s.find_last_not_of(L" \t");
    return s.substr(first, last - first + 1);
}

static bool parseHotkey(const std::wstring& title, UINT& modifiers, UINT& key)
{
    const size_t tab = title.find(L'\t');
    if (tab == std::wstring::npos) return false;
    const std::wstring spec = trim(title.substr(tab + 1));
    if (spec.empty()) return false;
    modifiers = 0; key = 0;
    size_t start = 0;
    while (start <= spec.size()) {
        const size_t plus = spec.find(L'+', start);
        const std::wstring token = trim(spec.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start));
        std::wstring upper = token;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);
        if (upper == L"CTRL" || upper == L"CONTROL") modifiers |= MOD_CONTROL;
        else if (upper == L"SHIFT") modifiers |= MOD_SHIFT;
        else if (upper == L"ALT") modifiers |= MOD_ALT;
        else if (upper == L"TAB") key = VK_TAB;
        else if (token.size() == 1) key = static_cast<UINT>(towupper(token[0]));
        else if (upper.size() >= 2 && upper[0] == L'F') {
            const int fn = _wtoi(upper.c_str() + 1);
            if (fn >= 1 && fn <= 24) key = VK_F1 + fn - 1;
        }
        if (plus == std::wstring::npos) break;
        start = plus + 1;
    }
    // RegisterHotKey with no modifiers captures the key globally. Require an
    // explicit modifier so a plain Tab (or any other bare key) stays usable.
    return key != 0 && modifiers != 0;
}

static void registerHotkeyForItem(const std::wstring& title, int id)
{
    UINT modifiers = 0, key = 0;
    if (!parseHotkey(title, modifiers, key)) {
        if (title.find(L'\t') != std::wstring::npos)
            debugLog(L"Hotkey parse failed title='" + title + L"'");
        return;
    }
    if (!g_hotkeyWnd) {
        debugLog(L"Hotkey registration skipped (window unavailable) title='" + title + L"'");
        return;
    }
    SetLastError(ERROR_SUCCESS);
    if (!RegisterHotKey(g_hotkeyWnd, id, modifiers, key)) {
        const DWORD error = GetLastError();
        const std::wstring message = L"RegisterHotKey failed for '" + title +
            L"' (id=" + std::to_wstring(id) + L", modifiers=" + std::to_wstring(modifiers) +
            L", key=" + std::to_wstring(key) + L", error=" + std::to_wstring(error) + L")";
        OutputDebugStringW(message.c_str());
        debugLog(message);
    } else {
        debugLog(L"RegisterHotKey succeeded for '" + title + L"' (id=" +
                 std::to_wstring(id) + L", modifiers=" + std::to_wstring(modifiers) +
                 L", key=" + std::to_wstring(key) + L")");
    }
}

static void unregisterHotkeys()
{
    if (!g_hotkeyWnd) return;
    for (size_t i = 0; i < g_scriptPaths.size(); ++i)
        UnregisterHotKey(g_hotkeyWnd, g_cmdBase + static_cast<int>(i));
}

static bool createHotkeyWindow()
{
    if (g_hotkeyWnd) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = hotkeyWndProc; wc.hInstance = g_hModule;
    wc.lpszClassName = HOTKEY_CLASS_NAME;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        debugLog(L"createOutputPane RegisterClassExW failed error=" + std::to_wstring(GetLastError()));
        return false;
    }
    g_hotkeyWnd = CreateWindowExW(0, HOTKEY_CLASS_NAME, L"NppMenuScripts Hotkey", 0,
                                  0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hModule, nullptr);
    return g_hotkeyWnd != nullptr;
}

static void destroyHotkeyWindow()
{
    if (g_hotkeyWnd) { DestroyWindow(g_hotkeyWnd); g_hotkeyWnd = nullptr; }
    UnregisterClassW(HOTKEY_CLASS_NAME, g_hModule);
}

static void openScriptsFolder()
{
    if (g_processRunning.load()) {
        MessageBoxW(g_nppData._nppHandle,
                    tr("PleaseWait").c_str(),
                    (std::wstring(L"NppMenuScripts - ") + tr("OpenScriptsFolder")).c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring folder = g_scriptFolder.empty()
        ? resolveScriptFolder(workspaceMenuPath(), L"./script")
        : g_scriptFolder;
    if (folder.empty()) return;

    const DWORD attrs = GetFileAttributesW(folder.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        const int answer = MessageBoxW(
            g_nppData._nppHandle,
            tr("ScriptsFolderMissing").c_str(),
            (std::wstring(L"NppMenuScripts - ") + tr("OpenScriptsFolder")).c_str(),
            MB_YESNO | MB_ICONQUESTION);
        if (answer != IDYES) return;
        const int result = SHCreateDirectoryExW(g_nppData._nppHandle, folder.c_str(), nullptr);
        if (result != ERROR_SUCCESS && result != ERROR_FILE_EXISTS && result != ERROR_ALREADY_EXISTS) {
            MessageBoxW(g_nppData._nppHandle,
                        tr("CreateScriptsFolderFailed").c_str(),
                        (std::wstring(L"NppMenuScripts - ") + tr("OpenScriptsFolder")).c_str(), MB_OK | MB_ICONERROR);
            return;
        }
    }

    const HINSTANCE result = ShellExecuteW(
        g_nppData._nppHandle, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(g_nppData._nppHandle,
                    tr("OpenScriptsFolderFailed").c_str(),
                    (std::wstring(L"NppMenuScripts - ") + tr("OpenScriptsFolder")).c_str(), MB_OK | MB_ICONWARNING);
    }
}

static void openMenuJson()
{
    const std::wstring path = workspaceMenuPath();
    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(g_nppData._nppHandle,
                    tr("MenuJsonMissing").c_str(),
                    (std::wstring(L"NppMenuScripts - ") + tr("OpenMenuJson")).c_str(), MB_OK | MB_ICONERROR);
        return;
    }
    SendMessageW(g_nppData._nppHandle, NPPM_DOOPEN, 0,
                 reinterpret_cast<LPARAM>(path.c_str()));
}

static int CALLBACK workspaceFolderBrowseCallback(HWND hwnd, UINT message, LPARAM, LPARAM data)
{
    if (message == BFFM_INITIALIZED && data) {
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
    }
    return 0;
}

static void setWorkspaceFolder()
{
    if (g_processRunning.load()) {
        MessageBoxW(g_nppData._nppHandle, tr("PleaseWaitSetWorkspacePath").c_str(),
                    (std::wstring(L"NppMenuScripts - ") + tr("SetWorkspaceFolder")).c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring oldWorkspace = g_workspaceFolder.empty() ? loadConfiguredWorkspacePath() : g_workspaceFolder;
    BROWSEINFOW browse{};
    browse.hwndOwner = g_nppData._nppHandle;
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    const std::wstring dialogTitle = tr("SelectWorkspaceFolder");
    browse.lpszTitle = dialogTitle.c_str();
    browse.lpfn = workspaceFolderBrowseCallback;
    browse.lParam = reinterpret_cast<LPARAM>(oldWorkspace.c_str());
    PIDLIST_ABSOLUTE selectedItem = SHBrowseForFolderW(&browse);
    if (!selectedItem) return;

    wchar_t selectedPath[MAX_PATH * 4] = {};
    const bool gotPath = SHGetPathFromIDListW(selectedItem, selectedPath) != FALSE;
    CoTaskMemFree(selectedItem);
    if (!gotPath) return;

    const std::wstring selected = fullPath(selectedPath);
    if (selected.empty() || _wcsicmp(selected.c_str(), oldWorkspace.c_str()) == 0) return;
    if (isPathWithinDirectory(moduleDirectory(), selected)) {
        MessageBoxW(g_nppData._nppHandle, tr("WorkspaceInsidePluginFolder").c_str(),
                    (std::wstring(L"NppMenuScripts - ") + tr("SetWorkspaceFolder")).c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    if (!initializeWorkspaceContents(selected)) {
        MessageBoxW(g_nppData._nppHandle, tr("WorkspaceInitFailed").c_str(),
                    (std::wstring(L"NppMenuScripts - ") + tr("SetWorkspaceFolder")).c_str(), MB_OK | MB_ICONERROR);
        return;
    }

    const std::wstring newMenuPath = joinPath(selected, L"menu.json");
    if (!saveConfiguredWorkspacePath(selected)) {
        MessageBoxW(g_nppData._nppHandle, tr("WorkspacePathSaveFailed").c_str(),
                    (std::wstring(L"NppMenuScripts - ") + tr("SetWorkspaceFolder")).c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    removeMenus();
    g_workspaceFolder = selected;
    if (!installMenus(newMenuPath)) {
        removeMenus();
        g_workspaceFolder = oldWorkspace;
        saveConfiguredWorkspacePath(oldWorkspace);
        installMenus(joinPath(oldWorkspace, L"menu.json"));
        return;
    }
    setNppStatusText(trFormat("WorkspacePathSet", {{L"{path}", g_workspaceFolder}}));
}

static void rebuildMenu()
{
    if (g_processRunning.load()) {
        MessageBoxW(g_nppData._nppHandle,
                    tr("PleaseWaitRebuild").c_str(),
                    (std::wstring(L"NppMenuScripts - ") + tr("RebuildMenuToolbar")).c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring path = workspaceMenuPath();
    if (path.empty()) return;

    // installMenus() validates the new configuration before removing the current menu.
    if (!installMenus(path, true)) {
        installMenus(path, false);
    } else {
        setNppStatusText(trFormat("MenuRebuilt", {{L"{path}", g_menuJsonPath}}));
    }
}

static void showAbout()
{
    const HINSTANCE result = ShellExecuteW(
        g_nppData._nppHandle, L"open",
        L"https://github.com/seantw/NppMenuScripts",
        nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(g_nppData._nppHandle,
                    tr("OpenGithubFailed").c_str(),
                    L"NppMenuScripts", MB_OK | MB_ICONWARNING);
    }
}

static HWND currentScintilla()
{
    if (!g_nppData._nppHandle) return nullptr;
    int which = 0;
    SendMessageW(g_nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
    return which == 0 ? g_nppData._scintillaMainHandle : g_nppData._scintillaSecondHandle;
}

struct StyledOutputRun {
    std::wstring text;
    int ansiColor = -1;
};

static bool parseSgrParameters(const std::wstring& parameters, int& foregroundColor)
{
    std::vector<int> codes;
    size_t start = 0;
    for (size_t i = 0; i <= parameters.size(); ++i) {
        if (i != parameters.size() && parameters[i] != L';') continue;
        if (i == start) {
            codes.push_back(0);
        } else {
            int value = 0;
            for (size_t digit = start; digit < i; ++digit) {
                if (parameters[digit] < L'0' || parameters[digit] > L'9') return false;
                value = value * 10 + (parameters[digit] - L'0');
                if (value > 10000) return false;
            }
            codes.push_back(value);
        }
        start = i + 1;
    }

    for (size_t i = 0; i < codes.size(); ++i) {
        const int code = codes[i];
        if (code == 0 || code == 39) {
            foregroundColor = -1;
        } else if (code >= 30 && code <= 37) {
            foregroundColor = code - 30;
        } else if (code >= 90 && code <= 97) {
            foregroundColor = code - 90 + 8;
        } else if (code == 38 || code == 48 || code == 58) {
            // Consume extended foreground/background/underline colors, which
            // are intentionally unsupported, so their numeric components are
            // not mistaken for ordinary SGR attributes.
            if (i + 1 < codes.size() && codes[i + 1] == 5) i = std::min(i + 2, codes.size() - 1);
            else if (i + 1 < codes.size() && codes[i + 1] == 2) i = std::min(i + 4, codes.size() - 1);
        }
        // Background colors and other SGR attributes are ignored. The output
        // pane keeps its Notepad++ theme background and existing base styles.
    }
    return true;
}

static std::vector<StyledOutputRun> parseAnsiOutput(const std::wstring& input, int& foregroundColor)
{
    std::vector<StyledOutputRun> runs;
    std::wstring text;
    int runColor = foregroundColor;
    auto flush = [&]() {
        if (!text.empty()) {
            runs.push_back({std::move(text), runColor});
            text.clear();
        }
    };

    for (size_t i = 0; i < input.size();) {
        if (input[i] != L'\x1b') {
            text.push_back(input[i++]);
            continue;
        }
        ++i;
        if (i >= input.size()) break;
        if (input[i] == L'[') {
            const size_t parameterStart = ++i;
            while (i < input.size() && !(input[i] >= 0x40 && input[i] <= 0x7E)) ++i;
            if (i >= input.size()) break;
            const wchar_t finalByte = input[i];
            if (finalByte == L'm') {
                flush();
                parseSgrParameters(input.substr(parameterStart, i - parameterStart), foregroundColor);
                runColor = foregroundColor;
            }
            ++i;
        } else if (input[i] == L']') {
            ++i;
            while (i < input.size()) {
                const wchar_t c = input[i++];
                if (c == L'\a') break;
                if (c == L'\x1b' && i < input.size() && input[i] == L'\\') { ++i; break; }
            }
        } else {
            // Drop unsupported single-character escape sequences.
            ++i;
        }
    }
    flush();
    return runs;
}

static std::string wideToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

static std::wstring normalizeOutputNewlines(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (c == L'\r') {
            out.push_back(L'\r');
            if (i + 1 < text.size() && text[i + 1] == L'\n') {
                out.push_back(L'\n');
                ++i;
            } else {
                out.push_back(L'\n');
            }
        } else if (c == L'\n') {
            out.append(L"\r\n");
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static void clearOutputPane()
{
    if (!g_outputEdit) return;
    debugLog(L"clearOutputPane edit=" + hexPtr(g_outputEdit));

    // Scintilla refuses document modifications while read-only is enabled.
    // The output pane is normally read-only, so temporarily unlock it.
    SendMessageW(g_outputEdit, 2171 /* SCI_SETREADONLY */, FALSE, 0);
    SendMessageW(g_outputEdit, 2004 /* SCI_CLEARALL */, 0, 0);
    SendMessageW(g_outputEdit, 2171 /* SCI_SETREADONLY */, TRUE, 0);
}

static void appendOutput(const std::wstring& text, bool isStderr = false)
{
    if (!g_outputEdit || text.empty()) {
        debugLog(L"appendOutput skipped edit=" + hexPtr(g_outputEdit) + L" textEmpty=" + std::to_wstring(text.empty()));
        return;
    }
    debugLog(L"appendOutput input wchar=" + std::to_wstring(text.size()));
    int& foregroundColor = isStderr ? g_stderrAnsiColor : g_stdoutAnsiColor;
    std::vector<StyledOutputRun> runs = parseAnsiOutput(text, foregroundColor);
    for (auto& run : runs) run.text = normalizeOutputNewlines(run.text);
    runs.erase(std::remove_if(runs.begin(), runs.end(), [](const StyledOutputRun& run) {
        return run.text.empty();
    }), runs.end());
    if (runs.empty()) { debugLog(L"appendOutput filtered empty"); return; }

    // SCI_APPENDTEXT is a document modification and is ignored while the
    // Scintilla document is read-only. Temporarily unlock for each chunk.
    constexpr UINT SCI_GETLENGTH = 2006;
    constexpr UINT SCI_GOTOPOS = 2025;
    constexpr UINT SCI_ADDTEXT = 2001;
    constexpr UINT SCI_STARTSTYLING = 2032;
    constexpr UINT SCI_SETSTYLING = 2033;
    constexpr int STYLE_DEFAULT = 32;
    constexpr int STYLE_STDERR = 1;
    constexpr int STYLE_ANSI_BASE = 2;

    SendMessageW(g_outputEdit, 2171 /* SCI_SETREADONLY */, FALSE, 0);
    // Put the insertion point at EOF explicitly.  This avoids depending on
    // the initial caret position of the newly created Scintilla control.
    const LRESULT length = SendMessageW(g_outputEdit, SCI_GETLENGTH, 0, 0);
    debugLog(L"appendOutput before length=" + std::to_wstring(length));
    SendMessageW(g_outputEdit, SCI_GOTOPOS, static_cast<WPARAM>(length), 0);
    SendMessageW(g_outputEdit, SCI_STARTSTYLING, static_cast<WPARAM>(length), 0xFF);
    for (const auto& run : runs) {
        const std::string utf8 = wideToUtf8(run.text);
        if (utf8.empty()) continue;
        SendMessageW(g_outputEdit, SCI_ADDTEXT, static_cast<WPARAM>(utf8.size()),
                     reinterpret_cast<LPARAM>(utf8.data()));
        const int style = run.ansiColor >= 0 ? STYLE_ANSI_BASE + run.ansiColor
            : isStderr ? STYLE_STDERR : STYLE_DEFAULT;
        SendMessageW(g_outputEdit, SCI_SETSTYLING, static_cast<WPARAM>(utf8.size()), style);
    }

    // Move the caret to the end so the newest output stays visible.
    const LRESULT newLength = SendMessageW(g_outputEdit, SCI_GETLENGTH, 0, 0);
    debugLog(L"appendOutput after length=" + std::to_wstring(newLength));
    SendMessageW(g_outputEdit, SCI_GOTOPOS, static_cast<WPARAM>(newLength), 0);
    SendMessageW(g_outputEdit, 2169 /* SCI_SCROLLCARET */, 0, 0);
    SendMessageW(g_outputEdit, 2171 /* SCI_SETREADONLY */, TRUE, 0);
}

static void appendStopNotification(const std::wstring& text)
{
    if (!g_outputEdit || text.empty()) return;

    constexpr UINT SCI_GETLENGTH = 2006;
    constexpr UINT SCI_GOTOPOS = 2025;
    constexpr UINT SCI_ADDTEXT = 2001;
    constexpr UINT SCI_STARTSTYLING = 2032;
    constexpr UINT SCI_SETSTYLING = 2033;
    constexpr int STYLE_STOP_NOTIFICATION = 18;
    const std::string utf8 = wideToUtf8(normalizeOutputNewlines(text));
    if (utf8.empty()) return;

    SendMessageW(g_outputEdit, 2171 /* SCI_SETREADONLY */, FALSE, 0);
    const LRESULT length = SendMessageW(g_outputEdit, SCI_GETLENGTH, 0, 0);
    SendMessageW(g_outputEdit, SCI_GOTOPOS, static_cast<WPARAM>(length), 0);
    SendMessageW(g_outputEdit, SCI_STARTSTYLING, static_cast<WPARAM>(length), 0xFF);
    SendMessageW(g_outputEdit, SCI_ADDTEXT, static_cast<WPARAM>(utf8.size()),
                 reinterpret_cast<LPARAM>(utf8.data()));
    SendMessageW(g_outputEdit, SCI_SETSTYLING, static_cast<WPARAM>(utf8.size()),
                 STYLE_STOP_NOTIFICATION);
    const LRESULT newLength = SendMessageW(g_outputEdit, SCI_GETLENGTH, 0, 0);
    SendMessageW(g_outputEdit, SCI_GOTOPOS, static_cast<WPARAM>(newLength), 0);
    SendMessageW(g_outputEdit, 2169 /* SCI_SCROLLCARET */, 0, 0);
    SendMessageW(g_outputEdit, 2171 /* SCI_SETREADONLY */, TRUE, 0);
}

static bool setNppStatusText(const std::wstring& text)
{
    if (!g_nppData._nppHandle) return false;
    const LRESULT result = SendMessageW(g_nppData._nppHandle, NPPM_SETSTATUSBAR,
                                        STATUSBAR_DOC_TYPE,
                                        reinterpret_cast<LPARAM>(text.c_str()));
    debugLog(L"setNppStatusText result=" + std::to_wstring(result) + L" text=\"" + text + L"\"");
    return result != FALSE;
}

static bool setOutputPaneTitle(const std::wstring& text)
{
    g_outputTitle = text;
    if (!g_outputPane) return true;

    g_outputDock.pszName = g_outputTitle.c_str();
    const LRESULT result = SendMessageW(g_nppData._nppHandle, NPPM_DMMUPDATEDISPINFO, 0,
                                        reinterpret_cast<LPARAM>(g_outputPane));
    debugLog(L"setOutputPaneTitle result=" + std::to_wstring(result) + L" title=\"" + text + L"\"");
    return result != FALSE;
}

static bool postRunStatus(const std::wstring& text, bool restoreEditorFocus = false,
                          bool showStopNotification = false, bool updateOutputCaret = true)
{
    if (!g_hotkeyWnd) return false;
    auto* payload = new RunStatePayload{text, restoreEditorFocus, showStopNotification, updateOutputCaret};
    if (!PostMessageW(g_hotkeyWnd, WM_NPPMENUSCRIPTS_RUNSTATE, 0, reinterpret_cast<LPARAM>(payload))) {
        delete payload;
        return false;
    }
    return true;
}

static void showOutputPane()
{
    if (g_outputPane) {
        const LRESULT r = SendMessageW(g_nppData._nppHandle, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(g_outputPane));
        debugLog(L"showOutputPane pane=" + hexPtr(g_outputPane) + L" result=" + std::to_wstring(r));
        if (g_outputEdit) {
            ShowWindow(g_outputEdit, SW_SHOW);
            RECT er{};
            GetClientRect(g_outputEdit, &er);
            debugLog(L"showOutputPane edit visible=" + std::to_wstring(IsWindowVisible(g_outputEdit)) +
                     L" rect=" + std::to_wstring(er.right - er.left) + L"x" + std::to_wstring(er.bottom - er.top));
        }
    } else {
        debugLog(L"showOutputPane skipped: pane=null");
    }
}

static int readNppBorderWidth()
{
    // Notepad++ stores this under GUIConfig/ScintillaPrimaryView as borderWidth.
    // Prefer local-config mode when doLocalConf.xml exists beside notepad++.exe;
    // otherwise use the normal %APPDATA%\Notepad++\config.xml location.
    wchar_t exePath[MAX_PATH * 4] = {};
    DWORD n = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    if (!n) return 0;
    std::wstring exe(exePath, n);
    const size_t slash = exe.find_last_of(L"\\/");
    const std::wstring exeDir = slash == std::wstring::npos ? L"." : exe.substr(0, slash);

    std::wstring configPath;
    const std::wstring localMarker = exeDir + L"\\doLocalConf.xml";
    if (GetFileAttributesW(localMarker.c_str()) != INVALID_FILE_ATTRIBUTES) {
        configPath = exeDir + L"\\config.xml";
    } else {
        wchar_t appData[MAX_PATH * 4] = {};
        DWORD m = GetEnvironmentVariableW(L"APPDATA", appData, static_cast<DWORD>(std::size(appData)));
        if (!m || m >= std::size(appData)) return 0;
        configPath = std::wstring(appData, m) + L"\\Notepad++\\config.xml";
    }

    std::ifstream f(configPath.c_str(), std::ios::binary);
    if (!f) {
        debugLog(L"readNppBorderWidth config not found=" + configPath);
        return 0;
    }
    std::string xml((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::string view = "name=\"ScintillaPrimaryView\"";
    const size_t viewPos = xml.find(view);
    if (viewPos == std::string::npos) {
        debugLog(L"readNppBorderWidth ScintillaPrimaryView not found path=" + configPath);
        return 0;
    }
    const size_t end = xml.find('>', viewPos);
    if (end == std::string::npos) return 0;
    const size_t attr = xml.find("borderWidth=\"", viewPos);
    if (attr == std::string::npos || attr >= end) {
        debugLog(L"readNppBorderWidth borderWidth not found path=" + configPath);
        return 0;
    }
    const size_t valueStart = attr + std::strlen("borderWidth=\"");
    const size_t valueEnd = xml.find('"', valueStart);
    if (valueEnd == std::string::npos || valueEnd > end) return 0;
    const int value = std::clamp(std::atoi(xml.substr(valueStart, valueEnd - valueStart).c_str()), 0, 30);
    debugLog(L"readNppBorderWidth path=" + configPath + L" value=" + std::to_wstring(value));
    return value;
}

static void applyOutputPaneBorderLayout(HWND pane)
{
    if (!pane || !g_outputEdit) return;
    RECT rc{};
    GetClientRect(pane, &rc);
    const int b = std::max(0, g_outputBorderWidth);
    const int width = static_cast<int>(std::max(0L, (rc.right - rc.left) - b * 2));
    const int height = static_cast<int>(std::max(0L, (rc.bottom - rc.top) - b * 2));
    ShowWindow(g_outputEdit, SW_SHOW);
    SetWindowPos(g_outputEdit, nullptr, b, b, width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
}

static void setOutputCaretVisible(bool visible)
{
    if (!g_outputEdit) return;
    constexpr UINT SCI_SETCARETSTYLE = 2512;
    constexpr UINT SCI_GETCARETSTYLE = 2513;
    constexpr int CARETSTYLE_INVISIBLE = 0;
    constexpr int CARETSTYLE_LINE = 1;

    const int currentStyle = static_cast<int>(SendMessageW(
        g_outputEdit, SCI_GETCARETSTYLE, 0, 0));
    if (visible) {
        const int style = g_outputCaretStyleCaptured ? g_outputCaretStyle : CARETSTYLE_LINE;
        SendMessageW(g_outputEdit, SCI_SETCARETSTYLE, style, 0);
    } else {
        if (currentStyle != CARETSTYLE_INVISIBLE) {
            g_outputCaretStyle = currentStyle;
            g_outputCaretStyleCaptured = true;
        }
        SendMessageW(g_outputEdit, SCI_SETCARETSTYLE, CARETSTYLE_INVISIBLE, 0);
    }
}

static void applyOutputStyle()
{
    if (!g_outputEdit) { debugLog(L"applyOutputStyle skipped: edit=null"); return; }
    HWND sci = currentScintilla();
    if (!sci) { debugLog(L"applyOutputStyle skipped: current scintilla=null"); return; }

    g_outputBorderWidth = readNppBorderWidth();

    constexpr int STYLE_DEFAULT = 32;
    constexpr UINT SCI_STYLECLEARALL = 2050;
    constexpr UINT SCI_STYLESETFORE = 2051;
    constexpr UINT SCI_STYLESETBACK = 2052;
    constexpr UINT SCI_STYLESETBOLD = 2053;
    constexpr UINT SCI_STYLESETITALIC = 2054;
    constexpr UINT SCI_STYLESETSIZE = 2055;
    constexpr UINT SCI_STYLESETFONT = 2056;
    constexpr UINT SCI_STYLEGETFORE = 2481;
    constexpr UINT SCI_STYLEGETBACK = 2482;
    constexpr UINT SCI_STYLEGETBOLD = 2483;
    constexpr UINT SCI_STYLEGETITALIC = 2484;
    constexpr UINT SCI_STYLEGETSIZE = 2485;
    constexpr UINT SCI_STYLEGETFONT = 2486;
    constexpr UINT SCI_GETTECHNOLOGY = 2631;
    constexpr UINT SCI_SETTECHNOLOGY = 2630;
    constexpr UINT SCI_SETLEXER = 4001;
    constexpr UINT SCI_SETCODEPAGE = 2037;
    constexpr UINT SCI_SETREADONLY = 2171;
    constexpr UINT SCI_SETSEL = 2160;
    constexpr UINT SCI_SETWRAPMODE = 2268;
    constexpr UINT SCI_SETHSCROLLBAR = 2130;
    constexpr UINT SCI_SETSCROLLWIDTH = 2274;
    constexpr UINT SCI_SETSCROLLWIDTHTRACKING = 2516;
    constexpr UINT SCI_SETMARGINWIDTHN = 2242;
    constexpr UINT SCI_GETMARGINLEFT = 2156;
    constexpr UINT SCI_SETMARGINLEFT = 2155;
    constexpr UINT SCI_GETMARGINRIGHT = 2158;
    constexpr UINT SCI_SETMARGINRIGHT = 2157;
    constexpr UINT SCI_GETELEMENTCOLOUR = 2754;
    constexpr UINT SCI_SETELEMENTCOLOUR = 2753;
    constexpr UINT SCI_RESETELEMENTCOLOUR = 2755;
    constexpr UINT SCI_GETELEMENTISSET = 2756;
    constexpr UINT SCI_GETSELECTIONLAYER = 2762;
    constexpr UINT SCI_SETSELECTIONLAYER = 2763;
    constexpr int SC_ELEMENT_SELECTION_TEXT = 10;
    constexpr int SC_ELEMENT_SELECTION_BACK = 11;
    constexpr int SC_ELEMENT_SELECTION_INACTIVE_TEXT = 16;
    constexpr int SC_ELEMENT_SELECTION_INACTIVE_BACK = 17;
    constexpr UINT SCI_SETCARETFORE = 2069;

    // Read the active document's default style.  A freshly created Scintilla
    // may return an unset style value, so keep a visible fallback.
    const LRESULT docFore = SendMessageW(sci, SCI_STYLEGETFORE, STYLE_DEFAULT, 0);
    const LRESULT docBack = SendMessageW(sci, SCI_STYLEGETBACK, STYLE_DEFAULT, 0);
    g_outputFore = (docFore >= 0 && docFore <= 0xFFFFFF)
        ? static_cast<COLORREF>(docFore) : RGB(0, 0, 0);
    g_outputBack = (docBack >= 0 && docBack <= 0xFFFFFF)
        ? static_cast<COLORREF>(docBack) : RGB(255, 255, 255);
    g_outputTechnology = static_cast<int>(SendMessageW(sci, SCI_GETTECHNOLOGY, 0, 0));
    const LRESULT bold = SendMessageW(sci, SCI_STYLEGETBOLD, STYLE_DEFAULT, 0);
    const LRESULT italic = SendMessageW(sci, SCI_STYLEGETITALIC, STYLE_DEFAULT, 0);
    const LRESULT size = SendMessageW(sci, SCI_STYLEGETSIZE, STYLE_DEFAULT, 0);
    debugLog(L"applyOutputStyle source sci=" + hexPtr(sci) +
             L" fore=" + std::to_wstring(docFore) + L" back=" + std::to_wstring(docBack) +
             L" tech=" + std::to_wstring(g_outputTechnology) + L" bold=" + std::to_wstring(bold) +
             L" italic=" + std::to_wstring(italic) + L" size=" + std::to_wstring(size));

    char fontName[256] = {};
    SendMessageA(sci, SCI_STYLEGETFONT, STYLE_DEFAULT, reinterpret_cast<LPARAM>(fontName));

    // SCI_STYLECLEARALL copies STYLE_DEFAULT to all styles.  It therefore has
    // to happen before setting our final STYLE_DEFAULT values.
    SendMessageW(g_outputEdit, SCI_STYLECLEARALL, 0, 0);
    SendMessageW(g_outputEdit, SCI_SETCODEPAGE, 65001 /* SC_CP_UTF8 */, 0);
    SendMessageW(g_outputEdit, SCI_SETLEXER, 0 /* SCLEX_NULL */, 0);

    if (g_outputTechnology >= 0 && g_outputTechnology <= 4)
        SendMessageW(g_outputEdit, SCI_SETTECHNOLOGY, static_cast<WPARAM>(g_outputTechnology), 0);

    SendMessageW(g_outputEdit, SCI_STYLESETFORE, STYLE_DEFAULT, g_outputFore);
    SendMessageW(g_outputEdit, SCI_STYLESETBACK, STYLE_DEFAULT, g_outputBack);
    SendMessageW(g_outputEdit, SCI_STYLESETBOLD, STYLE_DEFAULT, bold);
    SendMessageW(g_outputEdit, SCI_STYLESETITALIC, STYLE_DEFAULT, italic);
    if (size > 0) SendMessageW(g_outputEdit, SCI_STYLESETSIZE, STYLE_DEFAULT, size);
    if (fontName[0])
        SendMessageA(g_outputEdit, SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<LPARAM>(fontName));

    // Keep stderr readable with both light and dark editor themes.
    constexpr int STYLE_STDERR = 1;
    constexpr int STYLE_ANSI_BASE = 2;
    const int backgroundBrightness =
        (GetRValue(g_outputBack) * 299 + GetGValue(g_outputBack) * 587 +
         GetBValue(g_outputBack) * 114) / 1000;
    const COLORREF stderrFore = backgroundBrightness > 128
        ? RGB(180, 0, 0) : RGB(255, 110, 110);
    SendMessageW(g_outputEdit, SCI_STYLESETFORE, STYLE_STDERR, stderrFore);

    const bool darkBackground = backgroundBrightness <= 128;
    const COLORREF ansiPalette[16] = {
        darkBackground ? RGB(150, 150, 150) : RGB(0, 0, 0),
        darkBackground ? RGB(255, 110, 110) : RGB(128, 0, 0),
        darkBackground ? RGB(130, 220, 130) : RGB(0, 128, 0),
        darkBackground ? RGB(255, 220, 110) : RGB(128, 96, 0),
        darkBackground ? RGB(130, 170, 255) : RGB(0, 0, 160),
        darkBackground ? RGB(220, 140, 255) : RGB(128, 0, 128),
        darkBackground ? RGB(100, 220, 220) : RGB(0, 128, 128),
        darkBackground ? RGB(235, 235, 235) : RGB(96, 96, 96),
        darkBackground ? RGB(190, 190, 190) : RGB(96, 96, 96),
        darkBackground ? RGB(255, 150, 150) : RGB(220, 0, 0),
        darkBackground ? RGB(160, 255, 160) : RGB(0, 160, 0),
        darkBackground ? RGB(255, 245, 150) : RGB(170, 125, 0),
        darkBackground ? RGB(170, 200, 255) : RGB(0, 0, 220),
        darkBackground ? RGB(245, 170, 255) : RGB(190, 0, 190),
        darkBackground ? RGB(140, 255, 255) : RGB(0, 150, 150),
        darkBackground ? RGB(255, 255, 255) : RGB(48, 48, 48)
    };
    for (int color = 0; color < 16; ++color)
        SendMessageW(g_outputEdit, SCI_STYLESETFORE, STYLE_ANSI_BASE + color, ansiPalette[color]);
    constexpr int STYLE_STOP_NOTIFICATION = 18;
    SendMessageW(g_outputEdit, SCI_STYLESETFORE, STYLE_STOP_NOTIFICATION, RGB(255, 255, 0));
    SendMessageW(g_outputEdit, SCI_STYLESETBACK, STYLE_STOP_NOTIFICATION, RGB(192, 0, 0));
    SendMessageW(g_outputEdit, SCI_STYLESETBOLD, STYLE_STOP_NOTIFICATION, FALSE);

    // Copy both active and inactive selection elements while preserving whether
    // the source explicitly sets a colour or relies on Scintilla's defaults.
    // Setting an unset element as colour 0 makes it fully transparent.
    auto copySelectionElement = [&](int element, const wchar_t* name) {
        const bool isSet = SendMessageW(sci, SCI_GETELEMENTISSET, element, 0) != 0;
        const LRESULT colour = SendMessageW(sci, SCI_GETELEMENTCOLOUR, element, 0);
        if (isSet)
            SendMessageW(g_outputEdit, SCI_SETELEMENTCOLOUR, element, colour);
        else
            SendMessageW(g_outputEdit, SCI_RESETELEMENTCOLOUR, element, 0);
        debugLog(L"applyOutputStyle selection element=" + std::wstring(name) +
                 L" isSet=" + std::to_wstring(isSet) + L" colour=" + std::to_wstring(colour));
    };
    copySelectionElement(SC_ELEMENT_SELECTION_TEXT, L"active text");
    copySelectionElement(SC_ELEMENT_SELECTION_BACK, L"active back");
    copySelectionElement(SC_ELEMENT_SELECTION_INACTIVE_TEXT, L"inactive text");
    copySelectionElement(SC_ELEMENT_SELECTION_INACTIVE_BACK, L"inactive back");
    const LRESULT selectionLayer = SendMessageW(sci, SCI_GETSELECTIONLAYER, 0, 0);
    if (selectionLayer >= 0)
        SendMessageW(g_outputEdit, SCI_SETSELECTIONLAYER, selectionLayer, 0);
    debugLog(L"applyOutputStyle selection layer=" + std::to_wstring(selectionLayer));

    // Match the active editor's blank margins around the text.
    const LRESULT marginLeft = SendMessageW(sci, SCI_GETMARGINLEFT, 0, 0);
    const LRESULT marginRight = SendMessageW(sci, SCI_GETMARGINRIGHT, 0, 0);
    SendMessageW(g_outputEdit, SCI_SETMARGINLEFT, 0, marginLeft >= 0 ? marginLeft : 1);
    SendMessageW(g_outputEdit, SCI_SETMARGINRIGHT, 0, marginRight >= 0 ? marginRight : 1);

    // Match the active editor's native border / client edge.
    const LONG_PTR sourceStyle = GetWindowLongPtrW(sci, GWL_STYLE);
    LONG_PTR outputStyle = GetWindowLongPtrW(g_outputEdit, GWL_STYLE);
    if (sourceStyle & WS_BORDER) outputStyle |= WS_BORDER;
    else outputStyle &= ~static_cast<LONG_PTR>(WS_BORDER);
    SetWindowLongPtrW(g_outputEdit, GWL_STYLE, outputStyle);
    const LONG_PTR sourceExStyle = GetWindowLongPtrW(sci, GWL_EXSTYLE);
    LONG_PTR outputExStyle = GetWindowLongPtrW(g_outputEdit, GWL_EXSTYLE);
    if (sourceExStyle & WS_EX_CLIENTEDGE) outputExStyle |= WS_EX_CLIENTEDGE;
    else outputExStyle &= ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE);
    SetWindowLongPtrW(g_outputEdit, GWL_EXSTYLE, outputExStyle);
    SetWindowPos(g_outputEdit, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    applyOutputPaneBorderLayout(g_outputPane);

    // This is a log pane rather than a source editor.
    for (int margin = 0; margin < 5; ++margin)
        SendMessageW(g_outputEdit, SCI_SETMARGINWIDTHN, margin, 0);
    SendMessageW(g_outputEdit, SCI_SETWRAPMODE, 1 /* SC_WRAP_WORD */, 0);
    SendMessageW(g_outputEdit, SCI_SETHSCROLLBAR, FALSE, 0);
    SendMessageW(g_outputEdit, SCI_SETSCROLLWIDTH, 1, 0);
    SendMessageW(g_outputEdit, SCI_SETSCROLLWIDTHTRACKING, FALSE, 0);
    SendMessageW(g_outputEdit, SCI_SETCARETFORE, g_outputFore, 0);
    SendMessageW(g_outputEdit, SCI_SETREADONLY, TRUE, 0);
    SendMessageW(g_outputEdit, SCI_SETSEL, 0, 0);
    setOutputCaretVisible(!g_processRunning.load());
    InvalidateRect(g_outputEdit, nullptr, TRUE);
}

static void resizeOutputEdit(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);

    if (g_outputEdit) {
        applyOutputPaneBorderLayout(hwnd);
    }
}

static LRESULT CALLBACK outputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        debugLog(L"outputWndProc WM_CREATE pane=" + hexPtr(hwnd));
        g_outputEdit = reinterpret_cast<HWND>(SendMessageW(
            g_nppData._nppHandle, NPPM_CREATESCINTILLAHANDLE, 0,
            reinterpret_cast<LPARAM>(hwnd)));
        debugLog(L"NPPM_CREATESCINTILLAHANDLE edit=" + hexPtr(g_outputEdit));
        if (!g_outputEdit) return -1;
        applyOutputStyle();

        RECT er{};
        GetClientRect(g_outputEdit, &er);
        wchar_t cls[64] = {};
        GetClassNameW(g_outputEdit, cls, ARRAYSIZE(cls));
        debugLog(L"diagnostic edit visible=" + std::to_wstring(IsWindowVisible(g_outputEdit)) +
                 L" enabled=" + std::to_wstring(IsWindowEnabled(g_outputEdit)) +
                 L" rect=" + std::to_wstring(er.right - er.left) + L"x" + std::to_wstring(er.bottom - er.top) +
                 L" class=" + std::wstring(cls));
        return 0;
    }
    case WM_SIZE:
        resizeOutputEdit(hwnd);
        return 0;
    case WM_DESTROY:
        g_outputEdit = nullptr;
        g_outputPane = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool createOutputPane()
{
    debugLog(L"createOutputPane enter pane=" + hexPtr(g_outputPane));
    if (g_outputPane) return true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = outputWndProc;
    wc.hInstance = g_hModule;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = OUTPUT_CLASS_NAME;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    g_outputPane = CreateWindowExW(0, OUTPUT_CLASS_NAME, g_outputTitle.c_str(),
                                   WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                   0, 0, 0, 0, g_nppData._nppHandle, nullptr, g_hModule, nullptr);
    if (!g_outputPane) { debugLog(L"createOutputPane CreateWindowExW failed error=" + std::to_wstring(GetLastError())); return false; }
    debugLog(L"createOutputPane created pane=" + hexPtr(g_outputPane));

    g_outputDock = {};
    g_outputDock.hClient = g_outputPane;
    g_outputDock.pszName = g_outputTitle.c_str();
    g_outputDock.dlgID = OUTPUT_DLG_ID;
    g_outputDock.uMask = DWS_DF_CONT_BOTTOM;
    g_outputDock.pszAddInfo = nullptr;
    g_outputDock.pszModuleName = PLUGIN_NAME;

    const LRESULT dockResult = SendMessageW(g_nppData._nppHandle, NPPM_DMMREGASDCKDLG, 0,
                      reinterpret_cast<LPARAM>(&g_outputDock));
    debugLog(L"NPPM_DMMREGASDCKDLG result=" + std::to_wstring(dockResult) +
             L" pane=" + hexPtr(g_outputPane) + L" edit=" + hexPtr(g_outputEdit));
    if (!dockResult) {
        DestroyWindow(g_outputPane);
        g_outputPane = nullptr;
        return false;
    }
    return true;
}

static void destroyOutputPane()
{
    if (!g_outputPane) return;
    SendMessageW(g_nppData._nppHandle, NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(g_outputPane));
    DestroyWindow(g_outputPane);
    g_outputPane = nullptr;
    g_outputEdit = nullptr;
    UnregisterClassW(OUTPUT_CLASS_NAME, g_hModule);
}

class CodePageStreamDecoder {
public:
    explicit CodePageStreamDecoder(UINT codePage = CP_UTF8) : _codePage(codePage) {}

    void feed(const char* data, size_t size, const std::function<void(const std::wstring&)>& emit)
    {
        _pending.append(data, size);
        if (_codePage == 1200 || _codePage == 1201) {
            size_t complete = _pending.size() & ~static_cast<size_t>(1);
            if (complete >= 2) {
                const wchar_t lastUnit = readUtf16Unit(_pending.data() + complete - 2);
                if (lastUnit >= 0xD800 && lastUnit <= 0xDBFF) complete -= 2;
            }
            if (complete == 0) return;
            std::wstring converted;
            converted.reserve(complete / 2);
            for (size_t i = 0; i < complete; i += 2)
                converted.push_back(readUtf16Unit(_pending.data() + i));
            emit(converted);
            _pending.erase(0, complete);
            return;
        }
        if (_codePage == 12000 || _codePage == 12001) {
            const size_t complete = _pending.size() & ~static_cast<size_t>(3);
            if (complete == 0) return;
            std::wstring converted;
            converted.reserve(complete / 2);
            for (size_t i = 0; i < complete; i += 4) {
                uint32_t point = readUtf32CodePoint(_pending.data() + i);
                appendCodePoint(converted, point);
            }
            emit(converted);
            _pending.erase(0, complete);
            return;
        }
        if (_codePage == 65000) {
            // UTF-7 shifts can span arbitrary pipe reads. Decode it once at EOF.
            return;
        }
        if (_codePage != CP_UTF8) {
            size_t complete = _pending.size();
            if (complete != 0 && IsDBCSLeadByteEx(_codePage,
                    static_cast<BYTE>(_pending[complete - 1]))) --complete;
            if (complete == 0) return;
            const int needed = MultiByteToWideChar(_codePage, 0, _pending.data(),
                                                   static_cast<int>(complete), nullptr, 0);
            if (needed > 0) {
                std::wstring converted(static_cast<size_t>(needed), L'\0');
                MultiByteToWideChar(_codePage, 0, _pending.data(), static_cast<int>(complete),
                                    converted.data(), needed);
                emit(converted);
            }
            _pending.erase(0, complete);
            return;
        }
        size_t complete = 0;
        while (complete < _pending.size()) {
            const unsigned char c = static_cast<unsigned char>(_pending[complete]);
            size_t need = 1;
            if (c >= 0xC2 && c <= 0xDF) need = 2;
            else if (c >= 0xE0 && c <= 0xEF) need = 3;
            else if (c >= 0xF0 && c <= 0xF4) need = 4;
            else if (c >= 0x80 && c <= 0xBF) { ++complete; continue; }
            if (complete + need > _pending.size()) break;
            bool valid = true;
            for (size_t j = 1; j < need; ++j) {
                const unsigned char cc = static_cast<unsigned char>(_pending[complete + j]);
                if ((cc & 0xC0) != 0x80) { valid = false; break; }
            }
            if (!valid) { ++complete; continue; }
            complete += need;
        }
        if (complete == 0) return;
        const std::wstring w = utf8ChunkToWide(_pending.substr(0, complete));
        if (!w.empty()) emit(w);
        _pending.erase(0, complete);
    }

    void finish(const std::function<void(const std::wstring&)>& emit)
    {
        if (!_pending.empty()) {
            std::wstring w;
            if (_codePage == CP_UTF8) {
                w = utf8ChunkToWide(_pending);
            } else if (_codePage == 1200 || _codePage == 1201) {
                const size_t complete = _pending.size() & ~static_cast<size_t>(1);
                w.reserve(complete / 2);
                for (size_t i = 0; i < complete; i += 2)
                    w.push_back(readUtf16Unit(_pending.data() + i));
            } else if (_codePage == 12000 || _codePage == 12001) {
                const size_t complete = _pending.size() & ~static_cast<size_t>(3);
                w.reserve(complete / 2);
                for (size_t i = 0; i < complete; i += 4)
                    appendCodePoint(w, readUtf32CodePoint(_pending.data() + i));
            } else if (_codePage == 65000) {
                const int needed = MultiByteToWideChar(_codePage, 0, _pending.data(),
                                                       static_cast<int>(_pending.size()), nullptr, 0);
                if (needed > 0) {
                    w.resize(static_cast<size_t>(needed));
                    MultiByteToWideChar(_codePage, 0, _pending.data(),
                                        static_cast<int>(_pending.size()), w.data(), needed);
                }
            } else {
                const int needed = MultiByteToWideChar(_codePage, 0, _pending.data(),
                                                       static_cast<int>(_pending.size()), nullptr, 0);
                if (needed > 0) {
                    w.resize(static_cast<size_t>(needed));
                    MultiByteToWideChar(_codePage, 0, _pending.data(),
                                        static_cast<int>(_pending.size()), w.data(), needed);
                }
            }
            if (!w.empty()) emit(w);
            _pending.clear();
        }
    }
private:
    wchar_t readUtf16Unit(const char* bytes) const
    {
        const unsigned char first = static_cast<unsigned char>(bytes[0]);
        const unsigned char second = static_cast<unsigned char>(bytes[1]);
        return static_cast<wchar_t>(_codePage == 1200
            ? first | (static_cast<unsigned int>(second) << 8)
            : (static_cast<unsigned int>(first) << 8) | second);
    }

    uint32_t readUtf32CodePoint(const char* bytes) const
    {
        const unsigned char b0 = static_cast<unsigned char>(bytes[0]);
        const unsigned char b1 = static_cast<unsigned char>(bytes[1]);
        const unsigned char b2 = static_cast<unsigned char>(bytes[2]);
        const unsigned char b3 = static_cast<unsigned char>(bytes[3]);
        if (_codePage == 12000)
            return b0 | (static_cast<uint32_t>(b1) << 8) |
                   (static_cast<uint32_t>(b2) << 16) | (static_cast<uint32_t>(b3) << 24);
        return (static_cast<uint32_t>(b0) << 24) | (static_cast<uint32_t>(b1) << 16) |
               (static_cast<uint32_t>(b2) << 8) | b3;
    }

    static void appendCodePoint(std::wstring& output, uint32_t point)
    {
        if (point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) point = 0xFFFD;
        if (point <= 0xFFFF) {
            output.push_back(static_cast<wchar_t>(point));
        } else {
            point -= 0x10000;
            output.push_back(static_cast<wchar_t>(0xD800 + (point >> 10)));
            output.push_back(static_cast<wchar_t>(0xDC00 + (point & 0x3FF)));
        }
    }

    UINT _codePage;
    std::string _pending;
};

struct PipeReaderState {
    HANDLE pipe = nullptr;
    bool isStderr = false;
    bool open = true;
    UINT codePage = CP_UTF8;
    CodePageStreamDecoder decoder;
    std::string rawPending;
    std::string protocolPending;
};

static std::string encodeBase64(const std::string& input);
static std::string decodeBase64(const std::string& encoded, bool& ok);
static std::wstring getCurrentFilePath();
static std::wstring getFullPathFromBufferId(UINT_PTR bufferId);
static std::vector<UINT_PTR> getOpenBufferIds();
static std::pair<long, long> getCurrentCursor();

struct SystemDialogButtonTextState {
    std::wstring ok;
    std::wstring cancel;
};

static std::pair<std::wstring, std::wstring> getSystemDialogButtonTexts()
{
    // Read the standard MessageBox button captions directly from the localized
    // USER32 system resource.  Do not create a temporary MessageBox here: doing
    // so can activate/focus an unrelated window after the custom dialog closes.
    HMODULE user32Data = LoadLibraryExW(L"user32.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (user32Data) {
        wchar_t okBuffer[128] = {};
        wchar_t cancelBuffer[128] = {};

        const int okLength = LoadStringW(user32Data, 800, okBuffer,
                                         static_cast<int>(std::size(okBuffer)));
        const int cancelLength = LoadStringW(user32Data, 801, cancelBuffer,
                                             static_cast<int>(std::size(cancelBuffer)));

        FreeLibrary(user32Data);

        if (okLength > 0 && cancelLength > 0) {
            return { std::wstring(okBuffer, okLength),
                     std::wstring(cancelBuffer, cancelLength) };
        }
    }

    // Fallback only if the USER32 resources cannot be loaded.
    return { L"OK", L"Cancel" };
}

struct DialogIconSpec {
    UINT messageBoxFlag = 0;
    HICON handle = nullptr;
    bool ownsHandle = false;
};

static std::wstring lowerWide(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), ::towlower);
    return value;
}

static DialogIconSpec resolveDialogIcon(const std::wstring& requested, UINT defaultFlag)
{
    DialogIconSpec spec;
    spec.messageBoxFlag = defaultFlag;
    if (requested.empty()) return spec;

    const std::wstring name = lowerWide(requested);
    if (name == L"error" || name == L"x") {
        spec.messageBoxFlag = MB_ICONERROR;
        spec.handle = LoadIconW(nullptr, MAKEINTRESOURCEW(32513));
    } else if (name == L"warning" || name == L"!") {
        spec.messageBoxFlag = MB_ICONWARNING;
        spec.handle = LoadIconW(nullptr, MAKEINTRESOURCEW(32515));
    } else if (name == L"info" || name == L"i") {
        spec.messageBoxFlag = MB_ICONINFORMATION;
        spec.handle = LoadIconW(nullptr, MAKEINTRESOURCEW(32516));
    } else if (name == L"question" || name == L"?") {
        spec.messageBoxFlag = MB_ICONQUESTION;
        spec.handle = LoadIconW(nullptr, MAKEINTRESOURCEW(32514));
    } else {
        const std::wstring iconPath = joinPath(g_iconFolder, requested);
        spec.handle = static_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON,
                                                   0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
        if (!spec.handle) {
            postOutputMessage(L"[NppMenuScripts] Unable to load dialog icon: " + iconPath + L"\r\n", true);
            spec.messageBoxFlag = defaultFlag;
            return spec;
        }
        spec.ownsHandle = true;
    }
    return spec;
}

static void releaseDialogIcon(DialogIconSpec& spec)
{
    if (spec.ownsHandle && spec.handle) DestroyIcon(spec.handle);
    spec.handle = nullptr;
    spec.ownsHandle = false;
}

static int showCustomIconAlert(const std::wstring& message, const std::wstring& title, HICON icon)
{
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = g_nppData._nppHandle;
    config.dwFlags = TDF_USE_HICON_MAIN;
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    config.pszWindowTitle = title.c_str();
    config.pszMainInstruction = message.c_str();
    config.hMainIcon = icon;
    int button = 0;
    const HRESULT result = TaskDialogIndirect(&config, &button, nullptr, nullptr);
    return SUCCEEDED(result)
        ? button
        : MessageBoxW(g_nppData._nppHandle, message.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

static int showCustomIconConfirm(const std::wstring& message, const std::wstring& title, HICON icon)
{
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = g_nppData._nppHandle;
    config.dwFlags = TDF_USE_HICON_MAIN;
    config.dwCommonButtons = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = title.c_str();
    config.pszMainInstruction = message.c_str();
    config.hMainIcon = icon;
    int button = IDCANCEL;
    const HRESULT result = TaskDialogIndirect(&config, &button, nullptr, nullptr);
    return SUCCEEDED(result)
        ? button
        : MessageBoxW(g_nppData._nppHandle, message.c_str(), title.c_str(), MB_OKCANCEL | MB_ICONQUESTION);
}

static bool showPromptDialog(const std::wstring& message, const std::wstring& defaultValue,
                             const std::wstring& title, std::wstring& value, HICON icon)
{
    struct PromptState {
        const std::wstring* message;
        const std::wstring* defaultValue;
        std::wstring* value;
        std::wstring okText;
        std::wstring cancelText;
        HFONT font = nullptr;
        HICON icon = nullptr;
    } state{ &message, &defaultValue, &value, {}, {}, nullptr, icon };

    static const int IDC_PROMPT_EDIT = 1001;
    static const int IDC_PROMPT_MESSAGE = 1002;
    static const int IDC_PROMPT_FOOTER = 1003;

    auto dialogProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
        PromptState* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, DWLP_USER));

        if (msg == WM_INITDIALOG) {
            state = reinterpret_cast<PromptState*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            const auto buttonTexts = getSystemDialogButtonTexts();
            state->okText = buttonTexts.first;
            state->cancelText = buttonTexts.second;

            // MessageBox uses the system message-box font, not the 9pt dialog-template font.
            // Use the same NONCLIENTMETRICS message font here.
            NONCLIENTMETRICSW ncm{};
            ncm.cbSize = sizeof(ncm);
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
                state->font = CreateFontIndirectW(&ncm.lfMessageFont);
            }
            if (!state->font) {
                state->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            }

            HDC dc = GetDC(hwnd);
            HFONT oldFont = state->font
                ? static_cast<HFONT>(SelectObject(dc, state->font))
                : nullptr;

            const int minWidth = 360;
            const int maxWidth = 560;
            const int contentWidth = maxWidth - 28 - (state->icon ? 44 : 0);

            RECT singleLineRect{ 0, 0, 0, 0 };
            DrawTextW(dc, state->message->c_str(), -1, &singleLineRect,
                      DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);

            int dialogWidth = static_cast<int>(singleLineRect.right) + 40 + (state->icon ? 44 : 0);
            dialogWidth = std::clamp(dialogWidth, minWidth, maxWidth);

            RECT wrappedRect{ 0, 0, contentWidth, 0 };
            DrawTextW(dc, state->message->c_str(), -1, &wrappedRect,
                      DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            const int messageHeight = std::max(
                20, static_cast<int>(wrappedRect.bottom));

            if (oldFont) SelectObject(dc, oldFont);
            ReleaseDC(hwnd, dc);

            const int iconInset = state->icon ? 44 : 0;
            const int actualContentWidth = dialogWidth - 28;
            const int contentTop = 16;
            const int messageEditGap = 16;
            const int editY = contentTop + messageHeight + messageEditGap;
            const int editHeight = 32;
            const int editFooterGap = 16;
            const int footerY = editY + editHeight + editFooterGap;
            const int buttonHeight = 27;
            const int footerHeight = buttonHeight + 31;
            const int buttonY = footerY + (footerHeight - buttonHeight) / 2;
            const int desiredClientHeight = footerY + footerHeight;

            const int buttonWidth = 78;
            const int buttonGap = 8;
            const int buttonsWidth = buttonWidth * 2 + buttonGap;
            const int buttonsX = dialogWidth - 20 - buttonsWidth;

            HWND messageCtrl = CreateWindowExW(
                0, L"STATIC", state->message->c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                14 + iconInset, contentTop, actualContentWidth - iconInset, messageHeight, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROMPT_MESSAGE)),
                g_hModule, nullptr);

            CreateWindowExW(0, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW,
                0, footerY, dialogWidth, footerHeight, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROMPT_FOOTER)),
                g_hModule, nullptr);

            if (state->icon) {
                HWND iconCtrl = CreateWindowExW(0, L"STATIC", L"",
                    WS_CHILD | WS_VISIBLE | SS_ICON,
                    14, std::max(0, contentTop + (messageHeight - 32) / 2), 32, 32,
                    hwnd, nullptr, g_hModule, nullptr);
                SendMessageW(iconCtrl, STM_SETICON, reinterpret_cast<WPARAM>(state->icon), 0);
            }

            HWND editCtrl = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", state->defaultValue->c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOHSCROLL,
                14 + iconInset, editY, dialogWidth - 20 - (14 + iconInset), editHeight, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROMPT_EDIT)),
                g_hModule, nullptr);
            HWND okCtrl = CreateWindowExW(
                0, L"BUTTON", state->okText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                buttonsX, buttonY, buttonWidth, buttonHeight, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                g_hModule, nullptr);

            HWND cancelCtrl = CreateWindowExW(
                0, L"BUTTON", state->cancelText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                buttonsX + buttonWidth + buttonGap, buttonY,
                buttonWidth, buttonHeight, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                g_hModule, nullptr);

            if (state->font) {
                SendMessageW(messageCtrl, WM_SETFONT,
                              reinterpret_cast<WPARAM>(state->font), TRUE);
                SendMessageW(editCtrl, WM_SETFONT,
                              reinterpret_cast<WPARAM>(state->font), TRUE);
                SendMessageW(okCtrl, WM_SETFONT,
                              reinterpret_cast<WPARAM>(state->font), TRUE);
                SendMessageW(cancelCtrl, WM_SETFONT,
                              reinterpret_cast<WPARAM>(state->font), TRUE);
            }
            RECT editFormatRect{};
            GetClientRect(editCtrl, &editFormatRect);
            editFormatRect.left = 6;
            editFormatRect.top = 4;
            editFormatRect.right = std::max(editFormatRect.left,
                                             editFormatRect.right - 6);
            editFormatRect.bottom = std::max(editFormatRect.top,
                                              editFormatRect.bottom - 4);
            SendMessageW(editCtrl, EM_SETRECTNP, 0,
                         reinterpret_cast<LPARAM>(&editFormatRect));

            // SetWindowPos() sizes the outer dialog window, while the coordinates
            // above are client coordinates. Convert the desired client size first.
            RECT windowRect{ 0, 0, dialogWidth, desiredClientHeight };
            DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
            DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
            AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);

            const int windowWidth = windowRect.right - windowRect.left;
            const int windowHeight = windowRect.bottom - windowRect.top;

            // Match MessageBoxW: center the prompt over the Notepad++ main window.
            // Because the final size is calculated above, compute the position after
            // resizing rather than relying on the dialog's initial template size.
            RECT ownerRect{};
            GetWindowRect(g_nppData._nppHandle, &ownerRect);
            const int ownerWidth = ownerRect.right - ownerRect.left;
            const int ownerHeight = ownerRect.bottom - ownerRect.top;
            const int x = ownerRect.left + (ownerWidth - windowWidth) / 2;
            const int y = ownerRect.top + (ownerHeight - windowHeight) / 2;

            SetWindowPos(hwnd, HWND_TOP, x, y, windowWidth, windowHeight,
                         SWP_NOACTIVATE);

            SendMessageW(editCtrl, EM_SETSEL, 0, -1);
            SetFocus(editCtrl);
            return FALSE;
        }

        if (msg == WM_COMMAND) {
            const int id = LOWORD(wParam);
            if (id == IDOK) {
                const int length = GetWindowTextLengthW(
                    GetDlgItem(hwnd, IDC_PROMPT_EDIT));
                std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
                if (length > 0) {
                    GetDlgItemTextW(hwnd, IDC_PROMPT_EDIT, buffer.data(), length + 1);
                    buffer.resize(static_cast<size_t>(length));
                } else {
                    buffer.clear();
                }
                *state->value = std::move(buffer);
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
        }

        if (msg == WM_CTLCOLORDLG) {
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        }
        if (msg == WM_CTLCOLORSTATIC) {
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        }

        if (msg == WM_DRAWITEM) {
            const DRAWITEMSTRUCT* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (draw && draw->CtlID == IDC_PROMPT_FOOTER) {
                FillRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_BTNFACE));
                return TRUE;
            }
        }

        if (msg == WM_NCDESTROY && state && state->font) {
            DeleteObject(state->font);
            state->font = nullptr;
        }

        return FALSE;
    };

    // Build only the dialog itself dynamically; child controls are created in WM_INITDIALOG.
    // Give it a generous initial client area; WM_INITDIALOG resizes the outer window
    // after measuring the actual controls.
    std::vector<BYTE> data(1024, 0);
    size_t pos = 0;
    auto putWord = [&](WORD v) {
        *reinterpret_cast<WORD*>(data.data() + pos) = v; pos += sizeof(WORD);
    };
    auto putDword = [&](DWORD v) {
        *reinterpret_cast<DWORD*>(data.data() + pos) = v; pos += sizeof(DWORD);
    };
    auto putString = [&](const wchar_t* text) {
        while (*text) { putWord(static_cast<WORD>(*text++)); }
        putWord(0);
    };

    putDword(DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU);
    putDword(WS_EX_DLGMODALFRAME);
    putWord(0);
    putWord(10); putWord(10); putWord(400); putWord(180);
    putWord(0);
    putWord(0);
    putString(title.c_str());

    return DialogBoxIndirectParamW(g_hModule,
                                   reinterpret_cast<const DLGTEMPLATE*>(data.data()),
                                   g_nppData._nppHandle,
                                   dialogProc,
                                   reinterpret_cast<LPARAM>(&state)) == IDOK;
}

static bool showConfirmDialog(const std::wstring& message, const std::wstring& title,
                              const DialogIconSpec& icon)
{
    const int result = icon.ownsHandle
        ? showCustomIconConfirm(message, title, icon.handle)
        : MessageBoxW(g_nppData._nppHandle, message.c_str(), title.c_str(),
                      MB_OKCANCEL | icon.messageBoxFlag);
    return result == IDOK;
}

static bool handleConfirmProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_CONFIRM:";
    if (line.rfind(prefix, 0) != 0) return false;

    const std::string payload = line.substr(prefix.size());
    const size_t separator1 = payload.find('|');
    if (separator1 == std::string::npos) return false;
    const size_t separator2 = payload.find('|', separator1 + 1);
    if (separator2 == std::string::npos) return false;

    bool messageOk = false, titleOk = false, iconOk = false;
    const std::string decodedMessage = decodeBase64(payload.substr(0, separator1), messageOk);
    const std::string decodedTitle = decodeBase64(payload.substr(separator1 + 1, separator2 - separator1 - 1), titleOk);
    const std::string decodedIcon = decodeBase64(payload.substr(separator2 + 1), iconOk);
    if (!messageOk || !titleOk || !iconOk) return false;

    const std::wstring message = utf8ToWide(decodedMessage.c_str());
    const std::wstring title = utf8ToWide(decodedTitle.c_str());
    DialogIconSpec icon = resolveDialogIcon(utf8ToWide(decodedIcon.c_str()), 0);
    auto showAndRespond = [message, title, icon]() mutable {
        const bool accepted = showConfirmDialog(message, title, icon);
        releaseDialogIcon(icon);
        std::lock_guard<std::mutex> lock(g_stdinMutex);
        if (g_stdinWrite) {
            const char* response = accepted ? "OK\n" : "CANCEL\n";
            DWORD written = 0;
            const BOOL ok = WriteFile(g_stdinWrite, response,
                                      static_cast<DWORD>(std::strlen(response)),
                                      &written, nullptr);
            FlushFileBuffers(g_stdinWrite);
            debugLog(L"protocol confirm response sent accepted=" + std::to_wstring(accepted) +
                     L" WriteFile=" + std::to_wstring(ok) +
                     L" bytes=" + std::to_wstring(written));
        }
    };
    try {
        std::thread(showAndRespond).detach();
    } catch (...) {
        showAndRespond();
    }
    return true;
}

static bool handlePromptProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_PROMPT:";
    if (line.rfind(prefix, 0) != 0) return false;

    const std::string payload = line.substr(prefix.size());
    const size_t separator1 = payload.find('|');
    if (separator1 == std::string::npos) return false;
    const size_t separator2 = payload.find('|', separator1 + 1);
    if (separator2 == std::string::npos) return false;

    const size_t separator3 = payload.find('|', separator2 + 1);
    if (separator3 == std::string::npos) return false;

    bool messageOk = false, defaultOk = false, titleOk = false, iconOk = false;
    const std::string decodedMessage = decodeBase64(payload.substr(0, separator1), messageOk);
    const std::string decodedDefault = decodeBase64(payload.substr(separator1 + 1, separator2 - separator1 - 1), defaultOk);
    const std::string decodedTitle = decodeBase64(payload.substr(separator2 + 1, separator3 - separator2 - 1), titleOk);
    const std::string decodedIcon = decodeBase64(payload.substr(separator3 + 1), iconOk);
    if (!messageOk || !defaultOk || !titleOk || !iconOk) return false;

    const std::wstring message = utf8ToWide(decodedMessage.c_str());
    const std::wstring defaultValue = utf8ToWide(decodedDefault.c_str());
    const std::wstring title = utf8ToWide(decodedTitle.c_str());
    DialogIconSpec icon = resolveDialogIcon(utf8ToWide(decodedIcon.c_str()), 0);
    auto showAndRespond = [message, defaultValue, title, icon]() mutable {
        std::wstring value;
        const bool accepted = showPromptDialog(message, defaultValue, title, value, icon.handle);
        releaseDialogIcon(icon);
        std::lock_guard<std::mutex> lock(g_stdinMutex);
        if (g_stdinWrite) {
            std::string response;
            if (accepted) {
                response = "OK:" + encodeBase64(debugWideToUtf8(value)) + "\n";
            } else {
                response = "CANCEL\n";
            }
            DWORD written = 0;
            const BOOL ok = WriteFile(g_stdinWrite, response.data(),
                                      static_cast<DWORD>(response.size()), &written, nullptr);
            FlushFileBuffers(g_stdinWrite);
            debugLog(L"protocol prompt response sent accepted=" + std::to_wstring(accepted) +
                     L" WriteFile=" + std::to_wstring(ok) +
                     L" bytes=" + std::to_wstring(written));
        }
    };
    try {
        std::thread(showAndRespond).detach();
    } catch (...) {
        showAndRespond();
    }
    return true;
}

static std::string decodeBase64(const std::string& encoded, bool& ok)
{
    std::string decoded;
    decoded.reserve((encoded.size() / 4) * 3 + 3);
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int val = 0;
    int bits = -8;
    ok = true;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        const char* p = std::strchr(alphabet, c);
        if (!p) { ok = false; return {}; }
        val = (val << 6) + static_cast<int>(p - alphabet);
        bits += 6;
        if (bits >= 0) {
            decoded.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return decoded;
}

static bool handleGetFileProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_GETFILE:";
    if (line != prefix) return false;

    const std::string response = "OK:" + encodeBase64(debugWideToUtf8(getCurrentFilePath())) + "\n";
    std::lock_guard<std::mutex> lock(g_stdinMutex);
    if (g_stdinWrite) {
        DWORD written = 0;
        const BOOL ok = WriteFile(g_stdinWrite, response.data(),
                                  static_cast<DWORD>(response.size()), &written, nullptr);
        FlushFileBuffers(g_stdinWrite);
        debugLog(L"protocol getFile response sent WriteFile=" + std::to_wstring(ok) +
                 L" bytes=" + std::to_wstring(written));
    }
    return true;
}

static bool writeProtocolResponse(const std::string& response)
{
    std::lock_guard<std::mutex> lock(g_stdinMutex);
    if (!g_stdinWrite) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(g_stdinWrite, response.data(), static_cast<DWORD>(response.size()), &written, nullptr);
    FlushFileBuffers(g_stdinWrite);
    return ok && written == response.size();
}

static bool handleSaveFileProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_SAVEFILE:";
    if (line != prefix) return false;

    bool saved = false;
    if (!getCurrentFilePath().empty()) {
        HWND sci = currentScintilla();
        if (sci) {
            const bool wasModified = SendMessageW(sci, SCI_GETMODIFY_LOCAL, 0, 0) != 0;
            if (!wasModified) {
                // File/Save on an unchanged named document is already successful.
                saved = true;
            } else {
                const LRESULT result = SendMessageW(
                    g_nppData._nppHandle, NPPM_SAVECURRENTFILE, 0, 0);
                const bool remainsModified = SendMessageW(sci, SCI_GETMODIFY_LOCAL, 0, 0) != 0;
                saved = result != FALSE || !remainsModified;
            }
        }
    }

    writeProtocolResponse(saved ? "OK\n" : "ERROR\n");
    debugLog(L"protocol saveFile result=" + std::to_wstring(saved));
    return true;
}

static bool handleOpenFileProtocolLine(const std::string& line)
{
    static const std::string openPrefix = "\x1eNPPMENUSCRIPTS_OPENFILE:";
    static const std::string backgroundPrefix = "\x1eNPPMENUSCRIPTS_OPENFILEINBACKGROUND:";
    const bool background = line.rfind(backgroundPrefix, 0) == 0;
    const std::string& prefix = background ? backgroundPrefix : openPrefix;
    if (!background && line.rfind(openPrefix, 0) != 0) return false;

    bool decodedOk = false;
    const std::string decodedPath = decodeBase64(line.substr(prefix.size()), decodedOk);
    bool opened = false;
    bool restored = true;
    UINT_PTR previousBufferId = 0;
    int previousView = 0;

    if (decodedOk && !decodedPath.empty()) {
        const std::wstring path = normalizeWindowsPathSeparators(utf8ToWide(decodedPath.c_str()));
        if (background) {
            previousBufferId = static_cast<UINT_PTR>(SendMessageW(
                g_nppData._nppHandle, NPPM_GETCURRENTBUFFERID_LOCAL, 0, 0));
            previousView = static_cast<int>(SendMessageW(
                g_nppData._nppHandle, NPPM_GETCURRENTVIEW_LOCAL, 0, 0));
        }

        opened = SendMessageW(g_nppData._nppHandle, NPPM_DOOPEN, 0,
                              reinterpret_cast<LPARAM>(path.c_str())) != FALSE;

        if (opened && background && previousBufferId) {
            const LRESULT position = SendMessageW(
                g_nppData._nppHandle, NPPM_GETPOSFROMBUFFERID_LOCAL,
                static_cast<WPARAM>(previousBufferId), static_cast<LPARAM>(previousView));
            if (position < 0) {
                restored = false;
            } else {
                const auto packedPosition = static_cast<UINT_PTR>(position);
                const int view = static_cast<int>((packedPosition >> 30) & 0x3);
                const int index = static_cast<int>(packedPosition & 0x3fffffff);
                restored = SendMessageW(g_nppData._nppHandle, NPPM_ACTIVATEDOC_LOCAL,
                                        static_cast<WPARAM>(view), static_cast<LPARAM>(index)) != FALSE;
            }
        }
    }

    const bool result = opened && restored;
    writeProtocolResponse(result ? "OK\n" : "ERROR\n");
    debugLog(L"protocol " + std::wstring(background ? L"openFileInBackground" : L"openFile") +
             L" result=" + std::to_wstring(result) +
             L" previousBufferId=" + std::to_wstring(previousBufferId) +
             L" restored=" + std::to_wstring(restored));
    return true;
}

static bool handleGetOpenFilesProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_GETOPENFILES:";
    if (line != prefix) return false;

    std::string payload = "OK:";
    bool first = true;
    std::vector<UINT_PTR> emittedBufferIds;
    for (const UINT_PTR bufferId : getOpenBufferIds()) {
        if (std::find(emittedBufferIds.begin(), emittedBufferIds.end(), bufferId) != emittedBufferIds.end())
            continue;
        emittedBufferIds.push_back(bufferId);
        const std::wstring path = getFullPathFromBufferId(bufferId);
        // Notepad++ may return an untitled tab's display label (for example,
        // "new 1") instead of an empty path. Only return actual absolute paths.
        if (!isAbsolutePath(path)) continue;
        if (!first) payload.push_back('|');
        payload += encodeBase64(debugWideToUtf8(path));
        first = false;
    }
    payload.push_back('\n');
    writeProtocolResponse(payload);
    return true;
}

static bool handleCloseFileProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_CLOSEFILE:";
    if (line.rfind(prefix, 0) != 0) return false;

    bool decodedOk = false;
    const std::string decodedPath = decodeBase64(line.substr(prefix.size()), decodedOk);
    if (!decodedOk || decodedPath.empty()) {
        writeProtocolResponse("ERROR\n");
        return true;
    }

    const UINT_PTR previousBufferId = static_cast<UINT_PTR>(SendMessageW(
        g_nppData._nppHandle, NPPM_GETCURRENTBUFFERID_LOCAL, 0, 0));
    const int previousView = static_cast<int>(SendMessageW(
        g_nppData._nppHandle, NPPM_GETCURRENTVIEW_LOCAL, 0, 0));
    const std::wstring path = normalizeWindowsPathSeparators(utf8ToWide(decodedPath.c_str()));
    const bool switched = SendMessageW(g_nppData._nppHandle, NPPM_SWITCHTOFILE, 0,
                                       reinterpret_cast<LPARAM>(path.c_str())) != FALSE;
    if (!switched) {
        writeProtocolResponse("ERROR\n");
        return true;
    }

    const UINT_PTR targetBufferId = static_cast<UINT_PTR>(SendMessageW(
        g_nppData._nppHandle, NPPM_GETCURRENTBUFFERID_LOCAL, 0, 0));
    const std::vector<UINT_PTR> buffersBefore = getOpenBufferIds();
    const size_t occurrencesBefore = static_cast<size_t>(std::count(
        buffersBefore.begin(), buffersBefore.end(), targetBufferId));
    SendMessageW(g_nppData._nppHandle, NPPM_MENUCOMMAND, 0, 41003 /* IDM_FILE_CLOSE */);
    const std::vector<UINT_PTR> buffersAfter = getOpenBufferIds();
    const size_t occurrencesAfter = static_cast<size_t>(std::count(
        buffersAfter.begin(), buffersAfter.end(), targetBufferId));
    const bool closed = targetBufferId != 0 && occurrencesAfter < occurrencesBefore;

    if (previousBufferId != 0 &&
        std::find(buffersAfter.begin(), buffersAfter.end(), previousBufferId) != buffersAfter.end()) {
        LRESULT position = SendMessageW(g_nppData._nppHandle, NPPM_GETPOSFROMBUFFERID_LOCAL,
                                        static_cast<WPARAM>(previousBufferId),
                                        static_cast<LPARAM>(previousView));
        if (position < 0) {
            const int otherView = previousView == MAIN_VIEW ? SUB_VIEW : MAIN_VIEW;
            position = SendMessageW(g_nppData._nppHandle, NPPM_GETPOSFROMBUFFERID_LOCAL,
                                    static_cast<WPARAM>(previousBufferId),
                                    static_cast<LPARAM>(otherView));
        }
        if (position >= 0) {
            const auto packedPosition = static_cast<UINT_PTR>(position);
            const int view = static_cast<int>((packedPosition >> 30) & 0x3);
            const int index = static_cast<int>(packedPosition & 0x3fffffff);
            SendMessageW(g_nppData._nppHandle, NPPM_ACTIVATEDOC_LOCAL,
                         static_cast<WPARAM>(view), static_cast<LPARAM>(index));
        }
    }

    writeProtocolResponse(closed ? "OK\n" : "ERROR\n");
    debugLog(L"protocol closeFile result=" + std::to_wstring(closed) +
             L" targetBufferId=" + std::to_wstring(targetBufferId) +
             L" occurrencesBefore=" + std::to_wstring(occurrencesBefore) +
             L" occurrencesAfter=" + std::to_wstring(occurrencesAfter));
    return true;
}

static bool handleSetStatusBarTextProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_SETSTATUSBARTEXT:";
    if (line.rfind(prefix, 0) != 0) return false;

    bool decodedOk = false;
    const std::string decoded = decodeBase64(line.substr(prefix.size()), decodedOk);
    const bool result = decodedOk && setNppStatusText(utf8ToWide(decoded.c_str()));
    if (result) g_scriptSetStatusBarText.store(true);
    writeProtocolResponse(result ? "OK\n" : "ERROR\n");
    debugLog(L"protocol setStatusBarText result=" + std::to_wstring(result));
    return true;
}

static bool handleSetOutputPaneTitleProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_SETOUTPUTPANETITLE:";
    if (line.rfind(prefix, 0) != 0) return false;

    bool decodedOk = false;
    const std::string decoded = decodeBase64(line.substr(prefix.size()), decodedOk);
    const bool result = decodedOk && setOutputPaneTitle(utf8ToWide(decoded.c_str()));
    writeProtocolResponse(result ? "OK\n" : "ERROR\n");
    debugLog(L"protocol setOutputPaneTitle result=" + std::to_wstring(result));
    return true;
}

static std::wstring getCurrentLineText()
{
    HWND sci = currentScintilla();
    if (!sci) return L"";
    const LRESULT line = SendMessageW(sci, SCI_LINEFROMPOSITION_LOCAL,
                                      static_cast<WPARAM>(SendMessageW(sci, SCI_GETCURRENTPOS_LOCAL, 0, 0)), 0);
    const LRESULT length = SendMessageW(sci, SCI_LINELENGTH_LOCAL, static_cast<WPARAM>(line), 0);
    if (length <= 0) return L"";
    std::string buffer(static_cast<size_t>(length) + 1, '\0');
    const LRESULT copied = SendMessageW(sci, SCI_GETLINE_LOCAL, static_cast<WPARAM>(line), reinterpret_cast<LPARAM>(buffer.data()));
    if (copied <= 0) return L"";
    buffer.resize(static_cast<size_t>(copied));
    while (!buffer.empty() && (buffer.back() == '\r' || buffer.back() == '\n')) buffer.pop_back();
    return scintillaBytesToWide(sci, buffer);
}

static bool replaceCurrentLine(const std::wstring& text)
{
    HWND sci = currentScintilla();
    if (!sci) return false;
    const LRESULT firstVisibleLine = SendMessageW(sci, SCI_GETFIRSTVISIBLELINE_LOCAL, 0, 0);
    const LRESULT xOffset = SendMessageW(sci, SCI_GETXOFFSET_LOCAL, 0, 0);
    const LRESULT position = SendMessageW(sci, SCI_GETCURRENTPOS_LOCAL, 0, 0);
    const LRESULT line = SendMessageW(sci, SCI_LINEFROMPOSITION_LOCAL, static_cast<WPARAM>(position), 0);
    const LRESULT start = SendMessageW(sci, SCI_POSITIONFROMLINE_LOCAL, static_cast<WPARAM>(line), 0);
    const LRESULT end = SendMessageW(sci, SCI_GETLINEENDPOSITION_LOCAL, static_cast<WPARAM>(line), 0);
    const std::string utf8 = debugWideToUtf8(text);
    SendMessageW(sci, SCI_SETSEL_LOCAL, static_cast<WPARAM>(start), static_cast<LPARAM>(end));
    SendMessageW(sci, SCI_REPLACESEL_LOCAL, 0, reinterpret_cast<LPARAM>(utf8.c_str()));
    SendMessageW(sci, SCI_SETFIRSTVISIBLELINE_LOCAL, static_cast<WPARAM>(firstVisibleLine), 0);
    SendMessageW(sci, SCI_SETXOFFSET_LOCAL, static_cast<WPARAM>(xOffset), 0);
    return true;
}

static std::wstring getSelectionText()
{
    HWND sci = currentScintilla();
    if (!sci) return L"";
    const LRESULT start = SendMessageW(sci, SCI_GETSELECTIONSTART_LOCAL, 0, 0);
    const LRESULT end = SendMessageW(sci, SCI_GETSELECTIONEND_LOCAL, 0, 0);
    if (start == end) return L"";
    const LRESULT length = SendMessageW(sci, SCI_GETSELTEXT_LOCAL, 0, 0);
    if (length <= 0) return L"";
    std::string buffer(static_cast<size_t>(length) + 1, '\0');
    const LRESULT copied = SendMessageW(sci, SCI_GETSELTEXT_LOCAL, 0, reinterpret_cast<LPARAM>(buffer.data()));
    if (copied <= 0) return L"";
    buffer.resize(static_cast<size_t>(copied));
    if (!buffer.empty() && buffer.back() == '\0') buffer.pop_back();
    return scintillaBytesToWide(sci, buffer);
}

static bool replaceSelectionText(const std::wstring& text)
{
    HWND sci = currentScintilla();
    if (!sci) return false;
    const LRESULT firstVisibleLine = SendMessageW(sci, SCI_GETFIRSTVISIBLELINE_LOCAL, 0, 0);
    const LRESULT xOffset = SendMessageW(sci, SCI_GETXOFFSET_LOCAL, 0, 0);
    const std::string utf8 = debugWideToUtf8(text);
    SendMessageW(sci, SCI_REPLACESEL_LOCAL, 0, reinterpret_cast<LPARAM>(utf8.c_str()));
    SendMessageW(sci, SCI_SETFIRSTVISIBLELINE_LOCAL, static_cast<WPARAM>(firstVisibleLine), 0);
    SendMessageW(sci, SCI_SETXOFFSET_LOCAL, static_cast<WPARAM>(xOffset), 0);
    return true;
}

static bool hasCurrentSelection()
{
    HWND sci = currentScintilla();
    if (!sci) return false;
    return SendMessageW(sci, SCI_GETSELECTIONSTART_LOCAL, 0, 0) !=
           SendMessageW(sci, SCI_GETSELECTIONEND_LOCAL, 0, 0);
}

static std::wstring getEntireText()
{
    HWND sci = currentScintilla();
    if (!sci) return L"";
    const LRESULT length = SendMessageW(sci, SCI_GETTEXTLENGTH_LOCAL, 0, 0);
    if (length <= 0) return L"";
    std::string buffer(static_cast<size_t>(length) + 1, '\0');
    SendMessageW(sci, SCI_GETTEXT_LOCAL, static_cast<WPARAM>(buffer.size()), reinterpret_cast<LPARAM>(buffer.data()));
    return utf8ToWide(buffer.c_str());
}

static bool replaceEntireText(const std::wstring& text)
{
    HWND sci = currentScintilla();
    if (!sci) return false;
    const LRESULT firstVisibleLine = SendMessageW(sci, SCI_GETFIRSTVISIBLELINE_LOCAL, 0, 0);
    const LRESULT xOffset = SendMessageW(sci, SCI_GETXOFFSET_LOCAL, 0, 0);
    const std::string utf8 = debugWideToUtf8(text);
    const LRESULT length = SendMessageW(sci, SCI_GETTEXTLENGTH_LOCAL, 0, 0);
    SendMessageW(sci, SCI_SETSEL_LOCAL, 0, static_cast<LPARAM>(length));
    SendMessageW(sci, SCI_REPLACESEL_LOCAL, 0, reinterpret_cast<LPARAM>(utf8.c_str()));
    SendMessageW(sci, SCI_SETFIRSTVISIBLELINE_LOCAL, static_cast<WPARAM>(firstVisibleLine), 0);
    SendMessageW(sci, SCI_SETXOFFSET_LOCAL, static_cast<WPARAM>(xOffset), 0);
    return true;
}

static bool handleTextProtocolLine(const std::string& line)
{
    auto splitPayload = [&](const std::string& prefix, std::string& payload) -> bool {
        if (line.rfind(prefix, 0) != 0) return false;
        payload = line.substr(prefix.size());
        return true;
    };

    std::string payload;
    if (splitPayload("\x1eNPPMENUSCRIPTS_GETLINE:", payload)) {
        const std::string response = "OK:" + encodeBase64(debugWideToUtf8(getCurrentLineText())) + "\n";
        writeProtocolResponse(response);
        return true;
    }
    if (splitPayload("\x1eNPPMENUSCRIPTS_SETLINE:", payload)) {
        bool ok = false;
        const std::string decoded = decodeBase64(payload, ok);
        const bool result = ok && replaceCurrentLine(utf8ToWide(decoded.c_str()));
        writeProtocolResponse(result ? "OK\n" : "ERROR\n");
        return true;
    }
    if (splitPayload("\x1eNPPMENUSCRIPTS_GETSELECTION:", payload)) {
        const std::string response = "OK:" + encodeBase64(debugWideToUtf8(getSelectionText())) + "\n";
        writeProtocolResponse(response);
        return true;
    }
    if (splitPayload("\x1eNPPMENUSCRIPTS_SETSELECTION:", payload)) {
        bool ok = false;
        const std::string decoded = decodeBase64(payload, ok);
        const bool result = ok && replaceSelectionText(utf8ToWide(decoded.c_str()));
        writeProtocolResponse(result ? "OK\n" : "ERROR\n");
        return true;
    }
    if (splitPayload("\x1eNPPMENUSCRIPTS_HASSELECTION:", payload)) {
        writeProtocolResponse(hasCurrentSelection() ? "OK:1\n" : "OK:0\n");
        return true;
    }
    if (splitPayload("\x1eNPPMENUSCRIPTS_GETTEXT:", payload)) {
        const std::string response = "OK:" + encodeBase64(debugWideToUtf8(getEntireText())) + "\n";
        writeProtocolResponse(response);
        return true;
    }
    if (splitPayload("\x1eNPPMENUSCRIPTS_SETTEXT:", payload)) {
        bool ok = false;
        const std::string decoded = decodeBase64(payload, ok);
        const bool result = ok && replaceEntireText(utf8ToWide(decoded.c_str()));
        writeProtocolResponse(result ? "OK\n" : "ERROR\n");
        return true;
    }
    return false;
}

static bool handleClipboardProtocolLine(const std::string& line)
{
    static const std::string getPrefix = "\x1eNPPMENUSCRIPTS_GETCLIPBOARD:";
    static const std::string setPrefix = "\x1eNPPMENUSCRIPTS_SETCLIPBOARD:";

    if (line == getPrefix || line == getPrefix + "TEXT" || line == getPrefix + "HTML") {
        const bool wantHtml = line == getPrefix + "HTML";
        if (wantHtml) {
            const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");
            std::string html;
            if (htmlFormat && OpenClipboard(g_nppData._nppHandle)) {
                HANDLE data = GetClipboardData(htmlFormat);
                if (data) {
                    const SIZE_T size = GlobalSize(data);
                    const char* bytes = static_cast<const char*>(GlobalLock(data));
                    if (bytes && size > 0) {
                        html.assign(bytes, static_cast<size_t>(size));
                        GlobalUnlock(data);
                    }
                }
                CloseClipboard();
            }

            auto parseOffset = [](std::string_view line, std::string_view key, size_t& result) {
                if (line.substr(0, key.size()) != key) return false;
                size_t at = key.size();
                while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) ++at;
                if (at == line.size() || line[at] == '-') return false;
                size_t value = 0;
                bool hasDigit = false;
                for (; at < line.size() && line[at] >= '0' && line[at] <= '9'; ++at) {
                    const size_t digit = static_cast<size_t>(line[at] - '0');
                    if (value > (std::numeric_limits<size_t>::max() - digit) / 10) return false;
                    value = value * 10 + digit;
                    hasDigit = true;
                }
                while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) ++at;
                if (!hasDigit || at != line.size()) return false;
                result = value;
                return true;
            };

            size_t startFragment = 0;
            size_t endFragment = 0;
            bool hasStart = false;
            bool hasEnd = false;
            size_t lineStart = 0;
            while (!html.empty() && lineStart < html.size()) {
                const size_t lineEnd = html.find('\n', lineStart);
                const size_t end = lineEnd == std::string::npos ? html.size() : lineEnd;
                const std::string_view headerLine(html.data() + lineStart, end - lineStart);
                size_t parsed = 0;
                if (parseOffset(headerLine, "StartFragment:", parsed)) {
                    startFragment = parsed;
                    hasStart = true;
                } else if (parseOffset(headerLine, "EndFragment:", parsed)) {
                    endFragment = parsed;
                    hasEnd = true;
                } else if (headerLine.empty() || headerLine[0] == '<' || headerLine.find(':') == std::string_view::npos) {
                    break;
                }
                lineStart = lineEnd == std::string::npos ? html.size() : lineEnd + 1;
            }

            std::string fragment;
            if (hasStart && hasEnd && startFragment <= endFragment && endFragment <= html.size()) {
                fragment.assign(html, startFragment, endFragment - startFragment);
            } else {
                const size_t marker = html.find("<!--StartFragment");
                const size_t markerEnd = marker == std::string::npos ? std::string::npos : html.find("-->", marker);
                const size_t endMarker = markerEnd == std::string::npos ? std::string::npos : html.find("<!--EndFragment", markerEnd + 3);
                if (markerEnd != std::string::npos && endMarker != std::string::npos)
                    fragment.assign(html, markerEnd + 3, endMarker - (markerEnd + 3));
            }
            writeProtocolResponse("OK:" + encodeBase64(fragment) + "\n");
            return true;
        }

        std::wstring text;
        if (!OpenClipboard(g_nppData._nppHandle)) {
            writeProtocolResponse("ERROR\n");
            return true;
        }

        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        if (data) {
            const wchar_t* clipboardText = static_cast<const wchar_t*>(GlobalLock(data));
            if (clipboardText) {
                text.assign(clipboardText);
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
        writeProtocolResponse("OK:" + encodeBase64(debugWideToUtf8(text)) + "\n");
        return true;
    }

    if (line.rfind(setPrefix, 0) == 0) {
        bool decodedOk = false;
        const std::string decoded = decodeBase64(line.substr(setPrefix.size()), decodedOk);
        if (!decodedOk) {
            writeProtocolResponse("ERROR\n");
            return true;
        }

        const std::wstring text = utf8ToWide(decoded.c_str());
        const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!memory) {
            writeProtocolResponse("ERROR\n");
            return true;
        }

        wchar_t* buffer = static_cast<wchar_t*>(GlobalLock(memory));
        if (!buffer) {
            GlobalFree(memory);
            writeProtocolResponse("ERROR\n");
            return true;
        }
        memcpy(buffer, text.c_str(), bytes);
        GlobalUnlock(memory);

        bool success = false;
        if (OpenClipboard(g_nppData._nppHandle)) {
            if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) {
                success = true;
                memory = nullptr; // The system owns the handle after SetClipboardData succeeds.
            }
            CloseClipboard();
        }
        if (memory) GlobalFree(memory);
        writeProtocolResponse(success ? "OK\n" : "ERROR\n");
        return true;
    }

    return false;
}

static bool isNppMenuScriptsCommand(UINT commandId)
{
    return std::find(g_cmdIds.begin(), g_cmdIds.end(), static_cast<int>(commandId)) != g_cmdIds.end();
}

static bool dispatchMenuCommandPath(const std::wstring& path, bool* selfReference = nullptr)
{
    if (selfReference) *selfReference = false;
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t separator = path.find(L'/', start);
        std::wstring part = path.substr(start, separator == std::wstring::npos
            ? std::wstring::npos : separator - start);
        const size_t first = part.find_first_not_of(L" \t\r\n");
        const size_t last = part.find_last_not_of(L" \t\r\n");
        if (first == std::wstring::npos) return false;
        parts.push_back(part.substr(first, last - first + 1));
        if (separator == std::wstring::npos) break;
        start = separator + 1;
    }

    auto normalizeMenuLabel = [](std::wstring label) {
        const size_t tab = label.find(L'\t');
        if (tab != std::wstring::npos) label.resize(tab);
        while (!label.empty() && (label.back() == L' ' || label.back() == L'\t' ||
                                  label.back() == L'\r' || label.back() == L'\n')) {
            label.pop_back();
        }
        const size_t length = label.size();
        if (length >= 4 && label[length - 4] == L'(' && label[length - 3] == L'&' &&
            label[length - 1] == L')') {
            label.resize(length - 4);
        }
        std::wstring normalized;
        normalized.reserve(label.size());
        for (size_t i = 0; i < label.size(); ++i) {
            if (label[i] == L'&') {
                if (i + 1 < label.size() && label[i + 1] == L'&') {
                    normalized.push_back(L'&');
                    ++i;
                }
                continue;
            }
            normalized.push_back(label[i]);
        }
        const size_t first = normalized.find_first_not_of(L" \t\r\n");
        const size_t last = normalized.find_last_not_of(L" \t\r\n");
        if (first == std::wstring::npos) return std::wstring();
        return normalized.substr(first, last - first + 1);
    };
    auto labelsEqual = [](const std::wstring& left, const std::wstring& right) {
        return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
                                    right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
    };

    HMENU currentMenu = reinterpret_cast<HMENU>(
        SendMessageW(g_nppData._nppHandle, NPPM_GETMENUHANDLE, NPPMAINMENU, 0));
    UINT commandId = 0;
    bool found = currentMenu != nullptr;
    for (size_t partIndex = 0; found && partIndex < parts.size(); ++partIndex) {
        const std::wstring wanted = normalizeMenuLabel(parts[partIndex]);
        int foundPosition = -1;
        MENUITEMINFOW foundInfo{};
        foundInfo.cbSize = sizeof(foundInfo);
        for (int i = 0; i < GetMenuItemCount(currentMenu); ++i) {
            const int labelLength = GetMenuStringW(currentMenu, static_cast<UINT>(i), nullptr, 0, MF_BYPOSITION);
            if (labelLength <= 0) continue;
            std::vector<wchar_t> labelBuffer(static_cast<size_t>(labelLength) + 1, L'\0');
            if (GetMenuStringW(currentMenu, static_cast<UINT>(i), labelBuffer.data(),
                               static_cast<int>(labelBuffer.size()), MF_BYPOSITION) == 0) continue;
            if (!labelsEqual(normalizeMenuLabel(labelBuffer.data()), wanted)) continue;
            if (foundPosition >= 0) { found = false; break; }
            foundPosition = i;
            foundInfo = {};
            foundInfo.cbSize = sizeof(foundInfo);
            foundInfo.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_STATE | MIIM_FTYPE;
            if (!GetMenuItemInfoW(currentMenu, static_cast<UINT>(i), TRUE, &foundInfo)) {
                found = false;
                break;
            }
        }
        if (!found || foundPosition < 0 ||
            (foundInfo.fState & (MFS_DISABLED | MFS_GRAYED)) != 0 ||
            (foundInfo.fType & MFT_SEPARATOR) != 0) {
            found = false;
            break;
        }
        if (partIndex + 1 == parts.size()) {
            if (foundInfo.hSubMenu || foundInfo.wID == 0) found = false;
            else commandId = foundInfo.wID;
        } else {
            currentMenu = foundInfo.hSubMenu;
            if (!currentMenu) found = false;
        }
    }
    if (!found || commandId == 0) return false;
    if (isNppMenuScriptsCommand(commandId)) {
        if (selfReference) *selfReference = true;
        return false;
    }
    SendMessageW(g_nppData._nppHandle, WM_COMMAND, MAKEWPARAM(commandId, 0), 0);
    return true;
}

static bool dispatchMenuCommandId(int commandId, bool* selfReference = nullptr)
{
    if (selfReference) *selfReference = false;
    if (commandId <= 0) return false;
    if (isNppMenuScriptsCommand(static_cast<UINT>(commandId))) {
        if (selfReference) *selfReference = true;
        return false;
    }
    return SendMessageW(g_nppData._nppHandle, NPPM_MENUCOMMAND, 0,
                        static_cast<LPARAM>(commandId)) != 0;
}

static bool dispatchMenuCommandTarget(const std::wstring& target, bool* selfReference = nullptr)
{
    if (selfReference) *selfReference = false;
    if (target.empty()) return false;
    bool digitsOnly = true;
    int commandId = 0;
    for (const wchar_t digit : target) {
        if (digit < L'0' || digit > L'9' || commandId > (INT_MAX - (digit - L'0')) / 10) {
            digitsOnly = false;
            break;
        }
        commandId = commandId * 10 + (digit - L'0');
    }
    if (digitsOnly) return dispatchMenuCommandId(commandId, selfReference);
    return dispatchMenuCommandPath(target, selfReference);
}

static bool handleRunMenuCommandProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_RUNMENUCOMMAND:";
    if (line.rfind(prefix, 0) != 0) return false;

    const std::string payload = line.substr(prefix.size());
    if (payload.empty()) {
        writeProtocolResponse("ERROR\n");
        return true;
    }

    if (payload.rfind("PATH:", 0) == 0) {
        bool decodedOk = false;
        const std::string decodedPath = decodeBase64(payload.substr(5), decodedOk);
        if (!decodedOk || decodedPath.empty() || decodedPath.find('\0') != std::string::npos) {
            writeProtocolResponse("ERROR\n");
            return true;
        }

        writeProtocolResponse(dispatchMenuCommandPath(utf8ToWide(decodedPath.c_str())) ? "OK\n" : "ERROR\n");
        return true;
    }

    int commandId = 0;
    for (const char digit : payload) {
        if (digit < '0' || digit > '9' || commandId > (INT_MAX - (digit - '0')) / 10) {
            writeProtocolResponse("ERROR\n");
            return true;
        }
        commandId = commandId * 10 + (digit - '0');
    }
    if (commandId <= 0) {
        writeProtocolResponse("ERROR\n");
        return true;
    }

    writeProtocolResponse(dispatchMenuCommandId(commandId) ? "OK\n" : "ERROR\n");
    return true;
}

static bool handleGetCursorProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_GETCURSOR:";
    if (line != prefix) return false;

    const auto [lineNumber, columnNumber] = getCurrentCursor();
    const std::string response = "OK:" + std::to_string(lineNumber) + "," +
                                 std::to_string(columnNumber) + "\n";
    std::lock_guard<std::mutex> lock(g_stdinMutex);
    if (g_stdinWrite) {
        DWORD written = 0;
        const BOOL ok = WriteFile(g_stdinWrite, response.data(),
                                  static_cast<DWORD>(response.size()), &written, nullptr);
        FlushFileBuffers(g_stdinWrite);
        debugLog(L"protocol getCursor response sent line=" + std::to_wstring(lineNumber) +
                 L" column=" + std::to_wstring(columnNumber) +
                 L" WriteFile=" + std::to_wstring(ok) +
                 L" bytes=" + std::to_wstring(written));
    }
    return true;
}

static bool handleSetCursorProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_SETCURSOR:";
    if (line.rfind(prefix, 0) != 0) return false;

    const std::string payload = line.substr(prefix.size());
    const size_t separator = payload.find(',');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= payload.size()) {
        writeProtocolResponse("ERROR\n");
        return true;
    }

    auto parseCoordinate = [](const std::string& value, int& result) -> bool {
        int parsed = 0;
        for (const char digit : value) {
            if (digit < '0' || digit > '9' || parsed > (INT_MAX - (digit - '0')) / 10) return false;
            parsed = parsed * 10 + (digit - '0');
        }
        result = parsed;
        return true;
    };

    int lineNumber = 0;
    int columnNumber = 0;
    if (!parseCoordinate(payload.substr(0, separator), lineNumber) ||
        !parseCoordinate(payload.substr(separator + 1), columnNumber)) {
        writeProtocolResponse("ERROR\n");
        return true;
    }

    HWND sci = currentScintilla();
    if (!sci || lineNumber >= SendMessageW(sci, SCI_GETLINECOUNT_LOCAL, 0, 0)) {
        writeProtocolResponse("ERROR\n");
        return true;
    }

    const LRESULT position = SendMessageW(sci, SCI_FINDCOLUMN_LOCAL,
                                           static_cast<WPARAM>(lineNumber),
                                           static_cast<LPARAM>(columnNumber));
    if (position < 0) {
        writeProtocolResponse("ERROR\n");
        return true;
    }
    SendMessageW(sci, SCI_GOTOPOS_LOCAL, static_cast<WPARAM>(position), 0);
    g_scriptSetCursor.store(true);
    writeProtocolResponse("OK\n");
    return true;
}

static bool handleGetScrollLineProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_GETSCROLLLINE:";
    if (line != prefix) return false;

    HWND sci = currentScintilla();
    if (!sci) {
        writeProtocolResponse("ERROR\n");
        return true;
    }
    const LRESULT displayLine = SendMessageW(sci, SCI_GETFIRSTVISIBLELINE_LOCAL, 0, 0);
    const LRESULT documentLine = SendMessageW(sci, SCI_DOCLINEFROMVISIBLE_LOCAL,
                                               static_cast<WPARAM>(displayLine), 0);
    writeProtocolResponse("OK:" + std::to_string(documentLine) + "\n");
    return true;
}

static bool handleSetScrollLineProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_SETSCROLLLINE:";
    if (line.rfind(prefix, 0) != 0) return false;

    const std::string payload = line.substr(prefix.size());
    if (payload.empty()) {
        writeProtocolResponse("ERROR\n");
        return true;
    }
    int lineNumber = 0;
    for (const char digit : payload) {
        if (digit < '0' || digit > '9' || lineNumber > (INT_MAX - (digit - '0')) / 10) {
            writeProtocolResponse("ERROR\n");
            return true;
        }
        lineNumber = lineNumber * 10 + (digit - '0');
    }

    HWND sci = currentScintilla();
    if (!sci || lineNumber >= SendMessageW(sci, SCI_GETLINECOUNT_LOCAL, 0, 0)) {
        writeProtocolResponse("ERROR\n");
        return true;
    }

    SendMessageW(sci, SCI_ENSUREVISIBLE_LOCAL, static_cast<WPARAM>(lineNumber), 0);
    if (!SendMessageW(sci, SCI_GETLINEVISIBLE_LOCAL, static_cast<WPARAM>(lineNumber), 0)) {
        writeProtocolResponse("ERROR\n");
        return true;
    }

    const LRESULT displayLine = SendMessageW(sci, SCI_VISIBLEFROMDOCLINE_LOCAL,
                                               static_cast<WPARAM>(lineNumber), 0);
    if (displayLine < 0) {
        writeProtocolResponse("ERROR\n");
        return true;
    }
    SendMessageW(sci, SCI_SETFIRSTVISIBLELINE_LOCAL, static_cast<WPARAM>(displayLine), 0);
    writeProtocolResponse("OK\n");
    return true;
}

static bool handleProtocolLine(const std::string& line)
{
    static const std::string prefix = "\x1eNPPMENUSCRIPTS_ALERT:";
    if (line.rfind(prefix, 0) != 0) return false;

    const std::string payload = line.substr(prefix.size());
    const size_t separator1 = payload.find('|');
    if (separator1 == std::string::npos) return false;
    const size_t separator2 = payload.find('|', separator1 + 1);
    if (separator2 == std::string::npos) return false;

    bool messageOk = false, titleOk = false, iconOk = false;
    const std::string decodedMessage = decodeBase64(payload.substr(0, separator1), messageOk);
    const std::string decodedTitle = decodeBase64(payload.substr(separator1 + 1, separator2 - separator1 - 1), titleOk);
    const std::string decodedIcon = decodeBase64(payload.substr(separator2 + 1), iconOk);
    if (!messageOk || !titleOk || !iconOk) return false;

    const std::wstring message = utf8ToWide(decodedMessage.c_str());
    const std::wstring title = utf8ToWide(decodedTitle.c_str());
    DialogIconSpec icon = resolveDialogIcon(utf8ToWide(decodedIcon.c_str()), MB_ICONINFORMATION);
    debugLog(L"protocol alert received message=" + message + L" title=" + title);
    // Keep the Notepad++ UI thread free while the dialog is open. This lets
    // queued Output Pane updates continue to render during an async alert.
    auto showAndRespond = [message, title, icon]() mutable {
        const int result = icon.ownsHandle
            ? showCustomIconAlert(message, title, icon.handle)
            : MessageBoxW(g_nppData._nppHandle, message.c_str(),
                          title.c_str(), MB_OK | icon.messageBoxFlag);
        releaseDialogIcon(icon);
        std::lock_guard<std::mutex> lock(g_stdinMutex);
        if (g_stdinWrite) {
            const char response[] = "OK\n";
            DWORD written = 0;
            const BOOL ok = WriteFile(g_stdinWrite, response,
                                      static_cast<DWORD>(sizeof(response) - 1), &written, nullptr);
            FlushFileBuffers(g_stdinWrite);
            // Keep stdin open so the same Node.js process can issue another alert.
            // The stdin pipe is closed only after the Node.js process has exited.
            debugLog(L"protocol alert response sent result=" + std::to_wstring(result) +
                     L" WriteFile=" + std::to_wstring(ok) +
                     L" bytes=" + std::to_wstring(written) + L" stdin kept open");
        }
    };
    try {
        std::thread(showAndRespond).detach();
    } catch (...) {
        // Preserve the alert behavior if the worker thread cannot be created.
        showAndRespond();
    }
    return true;
}

static bool dispatchProtocolLineToUi(const std::string& line)
{
    if (!g_hotkeyWnd || g_shuttingDown.load()) return false;

    auto request = std::make_shared<ProtocolDispatchRequest>();
    request->line = line;
    auto* holder = new std::shared_ptr<ProtocolDispatchRequest>(request);
    if (!PostMessageW(g_hotkeyWnd, WM_NPPMENUSCRIPTS_PROTOCOL, 0, reinterpret_cast<LPARAM>(holder))) {
        delete holder;
        return false;
    }

    std::unique_lock<std::mutex> lock(request->mutex);
    while (!request->done && !g_shuttingDown.load()) {
        request->cv.wait_for(lock, std::chrono::milliseconds(50));
    }
    if (g_shuttingDown.load() && !request->done) return false;
    return request->handled;
}

static void reportHelperApiMismatch()
{
    if (g_helperApiMismatchReported.exchange(true)) return;

    MessageBoxW(g_nppData._nppHandle, tr("HelperIncompatible").c_str(),
                L"NppMenuScripts - Incompatible Helper API", MB_OK | MB_ICONERROR);

    std::lock_guard<std::mutex> lock(g_processHandleMutex);
    if (g_processHandle) TerminateProcess(g_processHandle, ERROR_REVISION_MISMATCH);
}

static bool handleHelperApiCheckLine(const std::string& line)
{
    const std::string prefix(HELPER_API_CHECK_PREFIX);
    if (line.rfind(prefix, 0) != 0) return false;

    const std::string helperId = line.substr(prefix.size());
    if (helperId == std::to_string(HELPER_API_ID)) {
        g_helperApiVerified.store(true);
    } else {
        reportHelperApiMismatch();
    }
    return true;
}

static bool postOutputMessage(const std::wstring& text, bool isStderr)
{
    if (!g_hotkeyWnd) return false;
    auto* payload = new std::wstring(text);
    if (!PostMessageW(g_hotkeyWnd, WM_NPPMENUSCRIPTS_OUTPUT, isStderr ? 1 : 0,
                      reinterpret_cast<LPARAM>(payload))) {
        delete payload;
        return false;
    }
    return true;
}

static void finishFailedLaunch(const std::wstring& message, bool isExternal = false,
                               bool preserveOutputPane = false)
{
    const bool outputPosted = !preserveOutputPane && postOutputMessage(message);
    g_processRunning.store(false);
    const bool runStatePosted = postRunStatus(tr(isExternal ? "ExternalCommandFailed" : "ScriptFailed"),
                                              false, false, !preserveOutputPane);
    debugLog(L"finishFailedLaunch outputPosted=" + std::to_wstring(outputPosted) +
             L" runStatePosted=" + std::to_wstring(runStatePosted));
}

static void stopScript()
{
    if (!g_processRunning.load()) return;

    bool stopSent = false;
    {
        std::lock_guard<std::mutex> lock(g_processHandleMutex);
        if (!g_processHandle) {
            // The worker may still be creating the child process. It checks
            // this flag immediately after publishing the process handles.
            g_stopRequested.store(true);
            stopSent = true;
        } else {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(g_processHandle, &exitCode) && exitCode == STILL_ACTIVE) {
                if (g_processJob) {
                    stopSent = TerminateJobObject(g_processJob, ERROR_CANCELLED) != FALSE;
                } else {
                    stopSent = TerminateProcess(g_processHandle, ERROR_CANCELLED) != FALSE;
                }
                g_stopRequested.store(stopSent);
            }
        }
    }
    debugLog(L"Stop Script requested; termination sent=" + std::to_wstring(stopSent));
}

static void processPipeWide(PipeReaderState& state,
                            const std::shared_ptr<std::atomic<bool>>& hadOutput,
                            const std::wstring& text)
{
    if (text.empty()) return;
    const std::string utf8 = debugWideToUtf8(text);
    state.protocolPending.append(utf8);
    size_t pos = 0;
    for (;;) {
        const size_t nl = state.protocolPending.find('\n', pos);
        if (nl == std::string::npos) break;
        std::string line = state.protocolPending.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const bool protocolCandidate = line.rfind("\x1eNPPMENUSCRIPTS_", 0) == 0;
        if (protocolCandidate) {
            if (!dispatchProtocolLineToUi(line) && !g_shuttingDown.load()) {
                const bool posted = postOutputMessage(utf8ToWide(line.c_str()) + L"\n",
                                                      state.isStderr);
                hadOutput->store(true);
                debugLog(L"readPipes emitted malformed protocol line PostMessage=" +
                         std::to_wstring(posted));
            }
        } else {
            const bool posted = postOutputMessage(utf8ToWide(line.c_str()) + L"\n",
                                                  state.isStderr);
            hadOutput->store(true);
            debugLog(L"readPipes emitted wchar=" + std::to_wstring(line.size()) +
                     L" PostMessage=" + std::to_wstring(posted) +
                     L" stderr=" + std::to_wstring(state.isStderr));
        }
        pos = nl + 1;
    }
    if (pos != 0) state.protocolPending.erase(0, pos);
}

static void processPipeBytes(PipeReaderState& state,
                             const std::shared_ptr<std::atomic<bool>>& hadOutput,
                             const char* data, size_t size)
{
    state.decoder.feed(data, size, [&](const std::wstring& text) {
        processPipeWide(state, hadOutput, text);
    });
}

static size_t findPipeLineSize(const PipeReaderState& state)
{
    const std::string& bytes = state.rawPending;
    if (state.codePage == 1200 || state.codePage == 1201) {
        for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
            const bool lf = state.codePage == 1200
                ? static_cast<unsigned char>(bytes[i]) == '\n' && bytes[i + 1] == '\0'
                : bytes[i] == '\0' && static_cast<unsigned char>(bytes[i + 1]) == '\n';
            if (lf) return i + 2;
        }
        return std::string::npos;
    }
    if (state.codePage == 12000 || state.codePage == 12001) {
        for (size_t i = 0; i + 3 < bytes.size(); i += 4) {
            const bool lf = state.codePage == 12000
                ? static_cast<unsigned char>(bytes[i]) == '\n' && bytes[i + 1] == '\0' &&
                  bytes[i + 2] == '\0' && bytes[i + 3] == '\0'
                : bytes[i] == '\0' && bytes[i + 1] == '\0' && bytes[i + 2] == '\0' &&
                  static_cast<unsigned char>(bytes[i + 3]) == '\n';
            if (lf) return i + 4;
        }
        return std::string::npos;
    }
    const size_t newline = bytes.find('\n');
    return newline == std::string::npos ? newline : newline + 1;
}

static bool emitNextPipeLine(PipeReaderState& state,
                             const std::shared_ptr<std::atomic<bool>>& hadOutput)
{
    const size_t lineSize = findPipeLineSize(state);
    if (lineSize == std::string::npos) return false;
    processPipeBytes(state, hadOutput, state.rawPending.data(), lineSize);
    state.rawPending.erase(0, lineSize);
    return true;
}

static void finishPipe(PipeReaderState& state,
                       const std::shared_ptr<std::atomic<bool>>& hadOutput)
{
    while (emitNextPipeLine(state, hadOutput)) {}
    if (!state.rawPending.empty()) {
        processPipeBytes(state, hadOutput, state.rawPending.data(), state.rawPending.size());
        state.rawPending.clear();
    }
    state.decoder.finish([&](const std::wstring& text) {
        processPipeWide(state, hadOutput, text);
    });
    if (!state.protocolPending.empty()) {
        const bool posted = postOutputMessage(utf8ToWide(state.protocolPending.c_str()),
                                              state.isStderr);
        hadOutput->store(true);
        debugLog(L"readPipes emitted trailing text PostMessage=" + std::to_wstring(posted));
        state.protocolPending.clear();
    }
}

static void readPipes(HANDLE stdoutPipe, HANDLE stderrPipe,
                      const std::shared_ptr<std::atomic<bool>>& hadOutput,
                      UINT codePage = CP_UTF8)
{
    PipeReaderState streams[2];
    streams[0].decoder = CodePageStreamDecoder(codePage);
    streams[1].decoder = CodePageStreamDecoder(codePage);
    streams[0].codePage = codePage;
    streams[1].codePage = codePage;
    streams[0].pipe = stdoutPipe;
    streams[0].isStderr = false;
    streams[1].pipe = stderrPipe;
    streams[1].isStderr = true;
    debugLog(L"readPipes start stdout=" + hexPtr(stdoutPipe) +
             L" stderr=" + hexPtr(stderrPipe));

    char buffer[8192];
    size_t nextStream = 0;
    bool deferFirstStderr = true;
    auto hasCompleteLine = [](const PipeReaderState& state) {
        return findPipeLineSize(state) != std::string::npos;
    };
    while (streams[0].open || streams[1].open ||
           hasCompleteLine(streams[0]) || hasCompleteLine(streams[1])) {
        bool madeProgress = false;
        for (size_t offset = 0; offset < 2; ++offset) {
            PipeReaderState& state = streams[offset];
            if (!state.open) continue;

            DWORD available = 0;
            if (!PeekNamedPipe(state.pipe, nullptr, 0, nullptr, &available, nullptr)) {
                const DWORD error = GetLastError();
                debugLog(L"readPipes pipe closed error=" + std::to_wstring(error) +
                         L" stderr=" + std::to_wstring(state.isStderr));
                CloseHandle(state.pipe);
                state.pipe = nullptr;
                state.open = false;
                madeProgress = true;
                continue;
            }
            if (available == 0) continue;

            DWORD bytesRead = 0;
            const DWORD requestSize = std::min<DWORD>(available, sizeof(buffer));
            if (!ReadFile(state.pipe, buffer, requestSize, &bytesRead, nullptr) || bytesRead == 0) {
                const DWORD error = GetLastError();
                debugLog(L"readPipes ReadFile ended error=" + std::to_wstring(error) +
                         L" stderr=" + std::to_wstring(state.isStderr));
                CloseHandle(state.pipe);
                state.pipe = nullptr;
                state.open = false;
                madeProgress = true;
                continue;
            }

            state.rawPending.append(buffer, bytesRead);
            debugLog(L"readPipes read bytes=" + std::to_wstring(bytesRead) +
                     L" available=" + std::to_wstring(available) +
                     L" stderr=" + std::to_wstring(state.isStderr));
            madeProgress = true;
        }

        const bool stdoutReady = hasCompleteLine(streams[0]);
        const bool stderrReady = hasCompleteLine(streams[1]);
        if (stdoutReady && stderrReady) {
            emitNextPipeLine(streams[nextStream], hadOutput);
            nextStream = 1 - nextStream;
            deferFirstStderr = false;
            madeProgress = true;
        } else if (stdoutReady) {
            emitNextPipeLine(streams[0], hadOutput);
            nextStream = 1;
            madeProgress = true;
        } else if (stderrReady) {
            if (deferFirstStderr) {
                deferFirstStderr = false;
            } else {
                emitNextPipeLine(streams[1], hadOutput);
                nextStream = 0;
                madeProgress = true;
            }
        }
        if (!madeProgress) Sleep(1);
    }
    finishPipe(streams[0], hadOutput);
    finishPipe(streams[1], hadOutput);
    debugLog(L"readPipes end");
}

static std::wstring getCurrentFilePath()
{
    if (!g_nppData._nppHandle) return L"";

    const UINT_PTR bufferId = static_cast<UINT_PTR>(SendMessageW(
        g_nppData._nppHandle, NPPM_GETCURRENTBUFFERID_LOCAL, 0, 0));
    return getFullPathFromBufferId(bufferId);
}

static std::wstring getFullPathFromBufferId(UINT_PTR bufferId)
{
    if (!g_nppData._nppHandle || !bufferId) return L"";

    const LRESULT length = SendMessageW(
        g_nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID_LOCAL,
        bufferId, 0);
    if (length <= 0) return L"";

    std::wstring path(static_cast<size_t>(length) + 1, L'\0');
    const LRESULT copied = SendMessageW(
        g_nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID_LOCAL,
        bufferId, reinterpret_cast<LPARAM>(path.data()));
    if (copied < 0) return L"";
    path.resize(static_cast<size_t>(copied));
    return path;
}

static std::vector<UINT_PTR> getOpenBufferIds()
{
    std::vector<UINT_PTR> bufferIds;
    if (!g_nppData._nppHandle) return bufferIds;

    const int countTypes[] = {PRIMARY_VIEW, SECOND_VIEW};
    const int viewIds[] = {MAIN_VIEW, SUB_VIEW};
    for (size_t i = 0; i < std::size(countTypes); ++i) {
        const LRESULT count = SendMessageW(g_nppData._nppHandle, NPPM_GETNBOPENFILES,
                                           0, static_cast<LPARAM>(countTypes[i]));
        debugLog(L"open-file enumeration countType=" + std::to_wstring(countTypes[i]) +
                 L" view=" + std::to_wstring(viewIds[i]) +
                 L" count=" + std::to_wstring(count));
        for (LRESULT index = 0; index < count; ++index) {
            const UINT_PTR bufferId = static_cast<UINT_PTR>(SendMessageW(
                g_nppData._nppHandle, NPPM_GETBUFFERIDFROMPOS,
                static_cast<WPARAM>(index), static_cast<LPARAM>(viewIds[i])));
            if (bufferId) {
                bufferIds.push_back(bufferId);
                debugLog(L"open-file enumeration index=" + std::to_wstring(index) +
                         L" bufferId=" + std::to_wstring(bufferId) +
                         L" path='" + getFullPathFromBufferId(bufferId) + L"'");
            }
        }
    }
    return bufferIds;
}

static std::pair<long, long> getCurrentCursor()
{
    HWND sci = currentScintilla();
    if (!sci) return {0, 0};

    const LRESULT position = SendMessageW(sci, SCI_GETCURRENTPOS_LOCAL, 0, 0);
    const LRESULT line = SendMessageW(sci, SCI_LINEFROMPOSITION_LOCAL,
                                      static_cast<WPARAM>(position), 0);
    const LRESULT column = SendMessageW(sci, SCI_GETCOLUMN_LOCAL,
                                        static_cast<WPARAM>(position), 0);
    return {static_cast<long>(line), static_cast<long>(column)};
}

static std::string encodeBase64(const std::string& input)
{
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    for (size_t i = 0; i < input.size(); i += 3) {
        const unsigned int a = static_cast<unsigned char>(input[i]);
        const bool hasB = i + 1 < input.size();
        const bool hasC = i + 2 < input.size();
        const unsigned int b = hasB ? static_cast<unsigned char>(input[i + 1]) : 0;
        const unsigned int c = hasC ? static_cast<unsigned char>(input[i + 2]) : 0;
        const unsigned int triple = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(triple >> 18) & 0x3F]);
        output.push_back(alphabet[(triple >> 12) & 0x3F]);
        output.push_back(hasB ? alphabet[(triple >> 6) & 0x3F] : '=');
        output.push_back(hasC ? alphabet[triple & 0x3F] : '=');
    }
    return output;
}

static std::wstring quoteCommandArg(const std::wstring& arg)
{
    if (arg.empty()) return L"\"\"";
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'\"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

static std::vector<wchar_t> makeEnvironmentBlock(
    const std::wstring& menuTitle, bool isPython,
    const std::unordered_map<std::wstring, std::wstring>& additionalEnvironment = {})
{
    std::vector<std::wstring> entries;
    std::wstring existingPythonPath;
    if (isPython) {
        const DWORD needed = GetEnvironmentVariableW(L"PYTHONPATH", nullptr, 0);
        if (needed > 1) {
            existingPythonPath.resize(needed, L'\0');
            const DWORD copied = GetEnvironmentVariableW(L"PYTHONPATH", existingPythonPath.data(), needed);
            if (copied > 0 && copied < needed) existingPythonPath.resize(copied);
            else existingPythonPath.clear();
        }
    }
    LPWCH env = GetEnvironmentStringsW();
    if (env) {
        for (LPWCH p = env; *p; p += std::wcslen(p) + 1) {
            const std::wstring entry(p);
            if (_wcsnicmp(entry.c_str(), L"NPPMENUSCRIPTS_MENU_TITLE=", 26) == 0) continue;
            if (_wcsnicmp(entry.c_str(), L"NPPMENUSCRIPTS_MENU_TITLE_B64=", 30) == 0) continue;
            if (_wcsnicmp(entry.c_str(), L"NPPMENUSCRIPTS_HELPER_API_ID=", 29) == 0) continue;
            if (isPython && _wcsnicmp(entry.c_str(), L"PYTHONPATH=", 11) == 0) continue;
            entries.push_back(entry);
        }
        FreeEnvironmentStringsW(env);
    }
    entries.push_back(L"NPPMENUSCRIPTS_MENU_TITLE=" + menuTitle);
    entries.push_back(L"NPPMENUSCRIPTS_MENU_TITLE_B64=" + utf8ToWide(encodeBase64(debugWideToUtf8(menuTitle)).c_str()));
    entries.push_back(L"NPPMENUSCRIPTS_HELPER_API_ID=" + std::to_wstring(HELPER_API_ID));
    if (isPython) {
        std::wstring pythonPath = moduleDirectory();
        if (!existingPythonPath.empty()) pythonPath += L";" + existingPythonPath;
        entries.push_back(L"PYTHONPATH=" + pythonPath);
    }
    for (const auto& variable : additionalEnvironment) {
        const std::wstring prefix = variable.first + L"=";
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const std::wstring& entry) {
            return entry.size() >= prefix.size() &&
                   _wcsnicmp(entry.c_str(), prefix.c_str(), prefix.size()) == 0;
        }), entries.end());
        entries.push_back(prefix + variable.second);
    }
    std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });

    size_t total = 1;
    for (const auto& entry : entries) total += entry.size() + 1;
    std::vector<wchar_t> block(total, L'\0');
    wchar_t* out = block.data();
    for (const auto& entry : entries) {
        std::copy(entry.begin(), entry.end(), out);
        out += entry.size();
        *out++ = L'\0';
    }
    *out = L'\0';
    return block;
}

static void replaceExecuteVariable(std::wstring& text, const wchar_t* token,
                                   const std::wstring& value)
{
    size_t position = 0;
    const size_t tokenLength = std::wcslen(token);
    while ((position = text.find(token, position)) != std::wstring::npos) {
        text.replace(position, tokenLength, value);
        position += value.size();
    }
}

static std::wstring fileNameFromPath(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

static std::wstring fileExtensionFromPath(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    return dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)
        ? L"" : path.substr(dot);
}

static bool executeNeedsFilePath(const ExecuteOptions& options)
{
    static const wchar_t* tokens[] = {L"{FILE_PATH}", L"{FILE_NAME}", L"{FILE_DIR}", L"{FILE_EXT}"};
    for (const wchar_t* token : tokens) {
        if (options.command.find(token) != std::wstring::npos) return true;
        for (const auto& entry : options.environment)
            if (entry.second.find(token) != std::wstring::npos) return true;
    }
    return false;
}

static void resizeEnvironmentValue(std::wstring& value, size_t length)
{
    if (length < value.size() && length > 0 &&
        value[length - 1] >= 0xD800 && value[length - 1] <= 0xDBFF &&
        value[length] >= 0xDC00 && value[length] <= 0xDFFF) --length;
    value.resize(length);
}

static bool saveCurrentDocumentForExecute(bool needsFilePath, bool saveBeforeRun,
                                         std::wstring& currentPath)
{
    HWND sci = currentScintilla();
    const bool modified = sci && SendMessageW(sci, SCI_GETMODIFY_LOCAL, 0, 0) != 0;
    const bool unnamedPathRequired = needsFilePath && currentPath.empty();
    if ((saveBeforeRun && modified) || unnamedPathRequired) {
        const char* promptKey = unnamedPathRequired ? "CurrentFileUnnamed" : "CurrentFileModified";
        if (MessageBoxW(g_nppData._nppHandle, tr(promptKey).c_str(), L"NppMenuScripts",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) return false;
        SendMessageW(g_nppData._nppHandle, NPPM_SAVECURRENTFILE, 0, 0);
        currentPath = getCurrentFilePath();
        const bool stillModified = sci && SendMessageW(sci, SCI_GETMODIFY_LOCAL, 0, 0) != 0;
        if ((saveBeforeRun && stillModified) || (needsFilePath && currentPath.empty())) {
            MessageBoxW(g_nppData._nppHandle,
                        tr("CurrentFileSaveFailed").c_str(), L"NppMenuScripts",
                        MB_OK | MB_ICONWARNING);
            return false;
        }
    }
    return true;
}

static bool prepareExecuteOptions(ExecuteOptions& options, const std::wstring& currentPath,
                                 const std::wstring& menuTitle)
{
    const std::wstring fileName = fileNameFromPath(currentPath);
    const std::wstring fileDir = currentPath.empty() ? L"" : directoryName(currentPath);
    const std::wstring fileExt = fileExtensionFromPath(currentPath);
    const std::wstring pathValues[] = {currentPath, fileName, fileDir, fileExt};
    const wchar_t* pathTokens[] = {L"{FILE_PATH}", L"{FILE_NAME}", L"{FILE_DIR}", L"{FILE_EXT}"};
    for (size_t i = 0; i < std::size(pathTokens); ++i)
        replaceExecuteVariable(options.command, pathTokens[i], pathValues[i]);
    replaceExecuteVariable(options.command, L"{SCRIPT_FOLDER}", g_scriptFolder);

    const std::unordered_map<std::wstring, std::wstring> values = {
        {L"{FILE_PATH}", currentPath}, {L"{FILE_NAME}", fileName},
        {L"{FILE_DIR}", fileDir}, {L"{FILE_EXT}", fileExt},
        {L"{SCRIPT_FOLDER}", g_scriptFolder},
        {L"{LINE_TEXT}", getCurrentLineText()}, {L"{SELECT_TEXT}", getSelectionText()}
    };
    std::unordered_map<std::wstring, std::wstring> emptyValues;
    for (const auto& entry : options.environment) emptyValues.emplace(entry.first, L"");
    const std::vector<wchar_t> emptyEnvironment = makeEnvironmentBlock(menuTitle, false, emptyValues);
    if (emptyEnvironment.size() > 32767) {
        MessageBoxW(g_nppData._nppHandle, tr("EnvironmentBlockTooLarge").c_str(),
                    L"NppMenuScripts", MB_OK | MB_ICONWARNING);
        return false;
    }
    const size_t availableValueLength = 32767 - emptyEnvironment.size();
    size_t configuredValueLength = 0;
    bool needsTruncation = false;
    for (auto& entry : options.environment) {
        for (const auto& value : values)
            replaceExecuteVariable(entry.second, value.first.c_str(), value.second);
        const size_t maxLength = entry.first.size() + 1 >= 32767 ? 0 : 32767 - entry.first.size() - 1;
        needsTruncation = needsTruncation || entry.second.size() > maxLength;
        configuredValueLength += entry.second.size();
    }
    needsTruncation = needsTruncation || configuredValueLength > availableValueLength;
    if (needsTruncation) {
        if (MessageBoxW(g_nppData._nppHandle, tr("EnvironmentValueTooLong").c_str(),
                        L"NppMenuScripts", MB_OKCANCEL | MB_ICONWARNING) != IDOK) return false;
        for (auto& entry : options.environment) {
            const size_t maxLength = entry.first.size() + 1 >= 32767 ? 0 : 32767 - entry.first.size() - 1;
            if (entry.second.size() > maxLength) {
                const size_t oldLength = entry.second.size();
                resizeEnvironmentValue(entry.second, maxLength);
                configuredValueLength -= oldLength - entry.second.size();
            }
        }
        std::vector<std::wstring*> largestValues;
        for (auto& entry : options.environment)
            largestValues.push_back(&entry.second);
        std::sort(largestValues.begin(), largestValues.end(), [](const auto* a, const auto* b) {
            return a->size() > b->size();
        });
        for (auto& entry : largestValues) {
            if (configuredValueLength <= availableValueLength) break;
            const size_t excess = configuredValueLength - availableValueLength;
            const size_t removed = std::min(excess, entry->size());
            const size_t oldLength = entry->size();
            resizeEnvironmentValue(*entry, entry->size() - removed);
            configuredValueLength -= oldLength - entry->size();
        }
    }
    return true;
}

static void runNodeScript(const std::wstring& path, const std::wstring& menuTitle,
                          const ExecuteOptions* external = nullptr)
{
    debugLog(L"runNodeScript path=" + path);
    std::lock_guard<std::mutex> lock(g_processMutex);
    if (g_processRunning.load()) {
        MessageBoxW(g_nppData._nppHandle, tr("ScriptAlreadyRunning").c_str(),
                    L"NppMenuScripts", MB_OK | MB_ICONWARNING);
        return;
    }
    const bool isExternal = external != nullptr;
    const ExecuteOptions executeOptions = external ? *external : ExecuteOptions{};
    const bool updatesOutputPane = !isExternal || executeOptions.runIn == ExecuteRunIn::OutputPane;
    if (updatesOutputPane) {
        setOutputPaneTitle(g_outputDefaultTitle);
        g_outputShownForRun = false;
        g_stdoutAnsiColor = -1;
        g_stderrAnsiColor = -1;
        if (g_outputPane) {
            clearOutputPane();
            applyOutputStyle();
        }
    }
    if (isExternal && executeOptions.runIn == ExecuteRunIn::OutputPane) {
        if (!g_outputPane && !createOutputPane()) {
            debugLog(L"runNodeScript failed to create Output Pane for external command");
        } else {
            showOutputPane();
            g_outputShownForRun = true;
        }
    }
    g_scriptSetStatusBarText.store(false);
    g_scriptSetCursor.store(false);
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring scriptName = external ? menuTitle :
        slash == std::wstring::npos ? path : path.substr(slash + 1);
    setNppStatusText(trFormat("ScriptRunning", {{L"{name}", scriptName}}));
    g_stopRequested.store(false);
    g_processRunning.store(true);
    if (updatesOutputPane) setOutputCaretVisible(false);
    g_helperApiVerified.store(false);
    g_helperApiMismatchReported.store(false);

    if (g_processThread.joinable()) g_processThread.join();
    std::wstring lowerPath = path;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::towlower);
    const bool isPerl = !external && lowerPath.size() >= 3 && lowerPath.rfind(L".pl") == lowerPath.size() - 3;
    const bool isPython = !external && lowerPath.size() >= 3 && lowerPath.rfind(L".py") == lowerPath.size() - 3;
    const std::wstring scriptFolder = g_scriptFolder;
    g_processThread = std::thread([path, menuTitle, scriptFolder, isPerl, isPython,
                                   isExternal, executeOptions]() {
        const bool captureOutput = !isExternal ||
            executeOptions.runIn == ExecuteRunIn::OutputPane;
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE inRead = nullptr, inWrite = nullptr;
        HANDLE outRead = nullptr, outWrite = nullptr;
        HANDLE errRead = nullptr, errWrite = nullptr;
        if (captureOutput && (!CreatePipe(&inRead, &inWrite, &sa, 0) ||
            !CreatePipe(&outRead, &outWrite, &sa, 0) ||
            !CreatePipe(&errRead, &errWrite, &sa, 0))) {
            if (inRead) CloseHandle(inRead); if (inWrite) CloseHandle(inWrite);
            if (outRead) CloseHandle(outRead); if (outWrite) CloseHandle(outWrite);
            if (errRead) CloseHandle(errRead); if (errWrite) CloseHandle(errWrite);
            finishFailedLaunch(tr("UnableCreatePipes") + L"\r\n", isExternal,
                               isExternal && executeOptions.runIn == ExecuteRunIn::Console);
            return;
        }
        if (captureOutput) {
            SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
        }

        std::wstring command;
        if (isExternal) {
            command = executeOptions.command;
            if (executeOptions.runIn == ExecuteRunIn::Console) {
                command = L"cmd.exe /d /s /k \"chcp " + std::to_wstring(executeOptions.codePage) +
                    L" >nul & " + command + L"\"";
            } else {
                size_t commandStart = command.find_first_not_of(L" \t");
                if (commandStart == std::wstring::npos) commandStart = command.size();
                bool hasCallPrefix = false;
                if (_wcsnicmp(command.c_str() + commandStart, L"call", 4) == 0 &&
                    (commandStart + 4 == command.size() || command[commandStart + 4] == L' ' ||
                     command[commandStart + 4] == L'\t')) {
                    hasCallPrefix = true;
                    commandStart = command.find_first_not_of(L" \t", commandStart + 4);
                    if (commandStart == std::wstring::npos) commandStart = command.size();
                }
                const bool quotedExecutable = commandStart < command.size() && command[commandStart] == L'"';
                size_t executableEnd = std::wstring::npos;
                if (quotedExecutable)
                    executableEnd = command.find(L'"', commandStart + 1);
                else
                    executableEnd = command.find_first_of(L" \t", commandStart);
                const size_t executableStart = commandStart + (quotedExecutable ? 1 : 0);
                const std::wstring executable = command.substr(executableStart,
                    executableEnd == std::wstring::npos ? executableEnd : executableEnd - executableStart);
                std::wstring lowerExecutable = executable;
                std::transform(lowerExecutable.begin(), lowerExecutable.end(), lowerExecutable.begin(), ::towlower);
                if ((lowerExecutable.size() >= 4 && lowerExecutable.rfind(L".bat") == lowerExecutable.size() - 4) ||
                    (lowerExecutable.size() >= 4 && lowerExecutable.rfind(L".cmd") == lowerExecutable.size() - 4))
                    command = L"cmd.exe /d /s /c \"\"" + (hasCallPrefix ? command.substr(commandStart) : command) + L"\"";
            }
        } else if (isPerl) {
            command = L"perl.exe -I " + quoteCommandArg(directoryName(scriptFolder)) +
                L" -I " + quoteCommandArg(moduleDirectory()) +
                L" -e " + quoteCommandArg(L"$|=1; $0=shift @ARGV; do $0; die $@ if $@;") +
                L" " + quoteCommandArg(path);
        } else if (isPython) {
            command = L"py.exe -u -X utf8 " + quoteCommandArg(path);
        } else {
            const std::string menuTitleUtf8 = debugWideToUtf8(menuTitle);
            const std::string menuTitleBase64 = encodeBase64(menuTitleUtf8);
            command = L"node.exe " + quoteCommandArg(path) +
                L" --nppmenuscripts-menu-title-b64 " + quoteCommandArg(utf8ToWide(menuTitleBase64.c_str())) +
                L" --nppmenuscripts-helper-api-id " + quoteCommandArg(std::to_wstring(HELPER_API_ID));
        }
        std::vector<wchar_t> environment = makeEnvironmentBlock(
            menuTitle, isPython, isExternal ? executeOptions.environment :
                std::unordered_map<std::wstring, std::wstring>{});
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        if (captureOutput) {
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = inRead;
            si.hStdOutput = outWrite;
            si.hStdError = errWrite;
        }
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> cmd(command.begin(), command.end());
        cmd.push_back(L'\0');

        debugLog(L"CreateProcess command=" + command);
        const bool detachedConsole = isExternal && executeOptions.runIn == ExecuteRunIn::Console;
        HANDLE processJob = detachedConsole ? nullptr : CreateJobObjectW(nullptr, nullptr);
        if (processJob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
            jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(processJob, JobObjectExtendedLimitInformation,
                                         &jobInfo, sizeof(jobInfo))) {
                debugLog(L"SetInformationJobObject failed error=" + std::to_wstring(GetLastError()));
                CloseHandle(processJob);
                processJob = nullptr;
            }
        } else if (!detachedConsole) {
            debugLog(L"CreateJobObject failed error=" + std::to_wstring(GetLastError()));
        }

        DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED;
        if (captureOutput) creationFlags |= CREATE_NO_WINDOW;
        else if (isExternal && executeOptions.runIn == ExecuteRunIn::Console)
            creationFlags |= CREATE_NEW_CONSOLE;
        BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, captureOutput,
                                 creationFlags,
                                 environment.data(), nullptr, &si, &pi);
        CloseHandle(inRead);
        CloseHandle(outWrite);
        CloseHandle(errWrite);
        if (!ok) {
            const DWORD err = GetLastError();
            debugLog(L"CreateProcess FAILED error=" + std::to_wstring(err));
            const char* errorKey = isExternal ? "UnableStartCommand" :
                isPerl ? "UnableStartPerl" : isPython ? "UnableStartPython" : "UnableStartNode";
            std::wstring msg = trFormat(errorKey,
                                        {{L"{error}", std::to_wstring(err)}});
            CloseHandle(inWrite);
            CloseHandle(outRead); CloseHandle(errRead);
            if (processJob) CloseHandle(processJob);
            finishFailedLaunch(msg, isExternal, detachedConsole);
            return;
        }

        debugLog(L"CreateProcess OK process=" + hexPtr(pi.hProcess));
        if (processJob && !AssignProcessToJobObject(processJob, pi.hProcess)) {
            debugLog(L"AssignProcessToJobObject failed error=" + std::to_wstring(GetLastError()));
            CloseHandle(processJob);
            processJob = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(g_processHandleMutex);
            g_processHandle = pi.hProcess;
            g_processJob = processJob;
        }
        {
            std::lock_guard<std::mutex> lock(g_stdinMutex);
            g_stdinWrite = inWrite;
        }
        if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
            debugLog(L"ResumeThread failed error=" + std::to_wstring(GetLastError()));
            if (processJob) TerminateJobObject(processJob, ERROR_PROCESS_ABORTED);
            else TerminateProcess(pi.hProcess, ERROR_PROCESS_ABORTED);
            WaitForSingleObject(pi.hProcess, INFINITE);
            {
                std::lock_guard<std::mutex> lock(g_processHandleMutex);
                g_processHandle = nullptr;
                g_processJob = nullptr;
            }
            {
                std::lock_guard<std::mutex> lock(g_stdinMutex);
                if (g_stdinWrite) CloseHandle(g_stdinWrite);
                g_stdinWrite = nullptr;
            }
            CloseHandle(outRead); CloseHandle(errRead);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            if (processJob) CloseHandle(processJob);
            finishFailedLaunch(L"[NppMenuScripts] Unable to resume the script process.\r\n",
                               isExternal, detachedConsole);
            return;
        }
        if (g_stopRequested.load()) {
            const BOOL stopped = processJob
                ? TerminateJobObject(processJob, ERROR_CANCELLED)
                : TerminateProcess(pi.hProcess, ERROR_CANCELLED);
            if (!stopped) {
                debugLog(L"Deferred script termination failed error=" + std::to_wstring(GetLastError()));
                g_stopRequested.store(false);
            }
        }
        if (detachedConsole) {
            const bool stopped = g_stopRequested.exchange(false);
            {
                std::lock_guard<std::mutex> lock(g_processHandleMutex);
                if (g_processHandle == pi.hProcess) g_processHandle = nullptr;
                g_processJob = nullptr;
            }
            {
                std::lock_guard<std::mutex> lock(g_stdinMutex);
                if (g_stdinWrite) CloseHandle(g_stdinWrite);
                g_stdinWrite = nullptr;
            }
            if (inWrite) CloseHandle(inWrite);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            g_processRunning.store(false);
            postRunStatus(tr(stopped ? "ExternalCommandStopped" : "ExternalCommandStarted"),
                          false, false, false);
            debugLog(L"console command launched independently");
            return;
        }
        auto hadOutput = std::make_shared<std::atomic<bool>>(false);
        std::thread outputThread;
        if (captureOutput)
            outputThread = std::thread(readPipes, outRead, errRead, hadOutput,
                                       isExternal ? executeOptions.codePage : CP_UTF8);
        WaitForSingleObject(pi.hProcess, INFINITE);
        if (outputThread.joinable()) outputThread.join();
        {
            std::lock_guard<std::mutex> lock(g_stdinMutex);
            if (g_stdinWrite) {
                CloseHandle(g_stdinWrite);
                g_stdinWrite = nullptr;
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_processHandleMutex);
            if (g_processHandle == pi.hProcess) g_processHandle = nullptr;
            if (g_processJob == processJob) g_processJob = nullptr;
        }
        CloseHandle(pi.hThread);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        if (processJob) CloseHandle(processJob);
        debugLog(L"process finished exitCode=" + std::to_wstring(exitCode) +
                 L" hadOutput=" + std::to_wstring(hadOutput->load()));
        const ScriptExitState exitState = classifyScriptExit(
            exitCode, g_stopRequested.exchange(false), g_scriptSetStatusBarText.load());
        const bool restoreEditorFocus = g_scriptSetCursor.exchange(false);
        g_processRunning.store(false);
        if (exitState == ScriptExitState::CompletedWithCustomStatus) {
            postRunStatus(L"", restoreEditorFocus);
        } else {
            const char* statusKey = isExternal
                ? (exitState == ScriptExitState::Stopped ? "ExternalCommandStopped" :
                   exitState == ScriptExitState::Completed ? "ExternalCommandCompleted" : "ExternalCommandFailed")
                : (exitState == ScriptExitState::Stopped ? "ScriptStopped" :
                   exitState == ScriptExitState::Completed ? "ScriptCompleted" : "ScriptFailed");
            postRunStatus(tr(statusKey),
                          restoreEditorFocus, exitState == ScriptExitState::Stopped);
        }
    });
}

static bool createScriptTemplate(const std::wstring& scriptPath)
{
    const std::wstring parent = directoryName(scriptPath);
    if (!parent.empty() && parent != L".") {
        const DWORD attrs = GetFileAttributesW(parent.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            const int result = SHCreateDirectoryExW(g_nppData._nppHandle, parent.c_str(), nullptr);
            if (result != ERROR_SUCCESS && result != ERROR_FILE_EXISTS && result != ERROR_ALREADY_EXISTS) {
                return false;
            }
        }
    }

    std::ofstream f(scriptPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return false;

    const std::wstring lower = [&]() {
        std::wstring value = scriptPath;
        std::transform(value.begin(), value.end(), value.begin(), ::towlower);
        return value;
    }();
    const bool isMjs = lower.size() >= 4 && lower.rfind(L".mjs") == lower.size() - 4;
    const bool isPerl = lower.size() >= 3 && lower.rfind(L".pl") == lower.size() - 3;
    const bool isPython = lower.size() >= 3 && lower.rfind(L".py") == lower.size() - 3;
    const std::string text = isPython
        ? "import menu_helper as npp\n\nprint('Hello from Python')\n"
        : isPerl
        ? "use strict;\nuse warnings;\n\nuse MenuHelper ();\n\nprint \"Hello from Perl\\n\";\n"
        : isMjs
        ? "// NppMenuScripts API:\n// https://github.com/seantw/NppMenuScripts\n\nimport npp from '#menu-helper';\n\nconsole.log('Hello, World!');\n"
        : "// NppMenuScripts API:\n// https://github.com/seantw/NppMenuScripts\n\nconst npp = require('#menu-helper');\n\nconsole.log('Hello, World!');\n";
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return f.good();
}

static bool openOrCreateScript(const std::wstring& scriptPath)
{
    const DWORD attrs = GetFileAttributesW(scriptPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        SendMessageW(g_nppData._nppHandle, NPPM_DOOPEN, 0,
                     reinterpret_cast<LPARAM>(scriptPath.c_str()));
        return true;
    }

    const std::wstring fileName = scriptPath.substr(scriptPath.find_last_of(L"\\/") + 1);
    const std::wstring message = trFormat("CreateScriptPrompt", {{L"{name}", fileName}});
    if (MessageBoxW(g_nppData._nppHandle, message.c_str(),
                    L"NppMenuScripts", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return false;
    }

    if (!createScriptTemplate(scriptPath)) {
        MessageBoxW(g_nppData._nppHandle,
                    tr("CreateScriptFailed").c_str(),
                    L"NppMenuScripts", MB_OK | MB_ICONERROR);
        return false;
    }

    SendMessageW(g_nppData._nppHandle, NPPM_DOOPEN, 0,
                 reinterpret_cast<LPARAM>(scriptPath.c_str()));
    return true;
}

static void showScriptPath(int id, bool checkCtrlClick = true)
{
    const int index = id - g_cmdBase;
    if (index < 0 || index >= static_cast<int>(g_scriptPaths.size())) return;

    const MenuCommandAction action = g_scriptActions[static_cast<size_t>(index)];
    if (action == MenuCommandAction::StopScript) {
        stopScript();
        return;
    }

    if (action == MenuCommandAction::RunMenuCommand) {
        const std::wstring& target = g_scriptPaths[static_cast<size_t>(index)];
        bool selfReference = false;
        if (!dispatchMenuCommandTarget(target, &selfReference)) {
            debugLog(L"Menu action could not be resolved: MENU:" + target);
            MessageBoxW(g_nppData._nppHandle,
                        tr(selfReference ? "MenuCommandSelfInvoke" : "MenuCommandFailed").c_str(),
                        L"NppMenuScripts", MB_OK | MB_ICONWARNING);
        }
        return;
    }

    if (action == MenuCommandAction::RunCommand) {
        ExecuteOptions options = g_executeOptions[static_cast<size_t>(index)];
        std::wstring currentPath = getCurrentFilePath();
        const bool needsFilePath = executeNeedsFilePath(options);
        const std::wstring& title = g_scriptTitles[static_cast<size_t>(index)];
        if (!saveCurrentDocumentForExecute(needsFilePath, options.saveBeforeRun, currentPath) ||
            !prepareExecuteOptions(options, currentPath, title)) return;
        runNodeScript(L"", title, &options);
        return;
    }

    std::wstring scriptPath = g_scriptPaths[static_cast<size_t>(index)];
    const bool currentFile = action == MenuCommandAction::RunCurrentFile;

    if (currentFile) {
        scriptPath = getCurrentFilePath();
        if (scriptPath.empty()) {
            MessageBoxW(g_nppData._nppHandle, tr("CurrentFileUnnamed").c_str(),
                        L"NppMenuScripts", MB_OK | MB_ICONWARNING);
            return;
        }
        if (!hasScriptExtension(scriptPath)) {
            const std::wstring message = trFormat("CurrentFileUnsupported", {
                {L"{extension}", scriptExtension(scriptPath)}});
            MessageBoxW(g_nppData._nppHandle, message.c_str(), L"NppMenuScripts", MB_OK | MB_ICONWARNING);
            return;
        }
        HWND sci = currentScintilla();
        if (sci && SendMessageW(sci, SCI_GETMODIFY_LOCAL, 0, 0) != 0) {
            const int answer = MessageBoxW(g_nppData._nppHandle,
                tr("CurrentFileModified").c_str(), L"NppMenuScripts",
                MB_YESNO | MB_ICONQUESTION);
            if (answer != IDYES) return;
            SendMessageW(g_nppData._nppHandle, NPPM_SAVECURRENTFILE, 0, 0);
            if (SendMessageW(sci, SCI_GETMODIFY_LOCAL, 0, 0) != 0) {
                MessageBoxW(g_nppData._nppHandle, tr("CurrentFileSaveFailed").c_str(),
                            L"NppMenuScripts", MB_OK | MB_ICONWARNING);
                return;
            }
            // Save As may change the active document's path.
            scriptPath = getCurrentFilePath();
            if (scriptPath.empty() || !hasScriptExtension(scriptPath)) {
                const std::wstring message = scriptPath.empty()
                    ? tr("CurrentFileUnnamed")
                    : trFormat("CurrentFileUnsupported", {{L"{extension}", scriptExtension(scriptPath)}});
                MessageBoxW(g_nppData._nppHandle, message.c_str(), L"NppMenuScripts", MB_OK | MB_ICONWARNING);
                return;
            }
        }
    }

    // Hidden shortcut: Ctrl+click a script menu item opens the script in Notepad++
    // instead of executing it.
    if (action == MenuCommandAction::RunScript && checkCtrlClick &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        openOrCreateScript(scriptPath);
        return;
    }

    const DWORD attrs = GetFileAttributesW(scriptPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!openOrCreateScript(scriptPath)) return;
        return;
    }

    runNodeScript(scriptPath, g_scriptTitles[static_cast<size_t>(index)]);
}

static LRESULT CALLBACK hotkeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_HOTKEY) {
        const int id = static_cast<int>(wParam);
        debugLog(L"WM_HOTKEY received id=" + std::to_wstring(id) +
                 L" modifiers=" + std::to_wstring(LOWORD(lParam)) +
                 L" key=" + std::to_wstring(HIWORD(lParam)));
        if (id >= g_cmdBase && id < g_cmdBase + g_cmdCount) {
            // WM_HOTKEY is dispatched while its modifier is still down; do
            // not mistake Ctrl+E for the menu's Ctrl+click editing gesture.
            showScriptPath(id, false);
            return 0;
        }
    } else if (msg == WM_NPPMENUSCRIPTS_REFRESH_LANGUAGE) {
        // Notepad++ sends NPPN_NATIVELANGCHANGED while it is refreshing its UI.
        // Wait until that notification has unwound and the native language API
        // reports the settled language before updating our menu labels.
        g_languageRefreshPasses = 0;
        SetTimer(hwnd, NPPMENUSCRIPTS_LANGUAGE_TIMER, 150, nullptr);
        return 0;
    } else if (msg == WM_TIMER && wParam == NPPMENUSCRIPTS_LANGUAGE_TIMER) {
        KillTimer(hwnd, NPPMENUSCRIPTS_LANGUAGE_TIMER);
        loadTranslations();
        g_outputDefaultTitle = tr("OutputPane");
        if (g_outputDefaultTitle.empty()) g_outputDefaultTitle = L"Output Pane";
        setOutputPaneTitle(g_outputDefaultTitle);
        applyTranslatedPluginMenu();
        if (++g_languageRefreshPasses < 4)
            SetTimer(hwnd, NPPMENUSCRIPTS_LANGUAGE_TIMER, 300, nullptr);
        return 0;
    } else if (msg == WM_NPPMENUSCRIPTS_PROTOCOL) {
        auto* holder = reinterpret_cast<std::shared_ptr<ProtocolDispatchRequest>*>(lParam);
        if (holder) {
            std::shared_ptr<ProtocolDispatchRequest> request = *holder;
            delete holder;
            bool handled = handleHelperApiCheckLine(request->line);
            if (!handled && !g_helperApiVerified.load() &&
                request->line.rfind("\x1eNPPMENUSCRIPTS_", 0) == 0) {
                reportHelperApiMismatch();
                handled = true;
            }
            if (!handled) handled = handleProtocolLine(request->line);
            if (!handled) handled = handlePromptProtocolLine(request->line);
            if (!handled) handled = handleConfirmProtocolLine(request->line);
            if (!handled) handled = handleGetFileProtocolLine(request->line);
            if (!handled) handled = handleSaveFileProtocolLine(request->line);
            if (!handled) handled = handleOpenFileProtocolLine(request->line);
            if (!handled) handled = handleGetOpenFilesProtocolLine(request->line);
            if (!handled) handled = handleCloseFileProtocolLine(request->line);
            if (!handled) handled = handleSetStatusBarTextProtocolLine(request->line);
            if (!handled) handled = handleSetOutputPaneTitleProtocolLine(request->line);
            if (!handled) handled = handleGetCursorProtocolLine(request->line);
            if (!handled) handled = handleSetCursorProtocolLine(request->line);
            if (!handled) handled = handleGetScrollLineProtocolLine(request->line);
            if (!handled) handled = handleSetScrollLineProtocolLine(request->line);
            if (!handled) handled = handleTextProtocolLine(request->line);
            if (!handled) handled = handleClipboardProtocolLine(request->line);
            if (!handled) handled = handleRunMenuCommandProtocolLine(request->line);
            {
                std::lock_guard<std::mutex> lock(request->mutex);
                request->handled = handled;
                request->done = true;
            }
            request->cv.notify_one();
        }
        return 0;
    } else if (msg == WM_NPPMENUSCRIPTS_OUTPUT) {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
        debugLog(L"WM_NPPMENUSCRIPTS_OUTPUT received text=" + std::to_wstring(text ? text->size() : 0) +
                 L" pane=" + hexPtr(g_outputPane) + L" edit=" + hexPtr(g_outputEdit));
        if (text && !text->empty()) {
            if (!g_outputPane && !createOutputPane()) {
                debugLog(L"createOutputPane FAILED from output message");
                return 0;
            }
            appendOutput(*text, wParam != 0);
            if (!g_outputShownForRun) {
                showOutputPane();
                g_outputShownForRun = true;
            }
        }
        return 0;
    } else if (msg == WM_NPPMENUSCRIPTS_RUNSTATE) {
        std::unique_ptr<RunStatePayload> state(reinterpret_cast<RunStatePayload*>(lParam));
        if (state) {
            if (state->updateOutputCaret) setOutputCaretVisible(!g_processRunning.load());
            if (!state->text.empty()) setNppStatusText(state->text);
            if (state->showStopNotification) {
                if (g_outputPane && g_outputEdit && IsWindowVisible(g_outputPane) && IsWindowVisible(g_outputEdit)) {
                    appendStopNotification(state->text + L"\r\n");
                } else {
                    MessageBoxW(g_nppData._nppHandle, state->text.c_str(),
                                L"NppMenuScripts", MB_OK | MB_ICONWARNING);
                }
            }
            if (state->restoreEditorFocus) {
                HWND sci = currentScintilla();
                if (sci && IsWindow(sci)) SetFocus(sci);
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool addMenuItem(HMENU menu, const std::wstring& title, const std::wstring& path,
                        MenuCommandAction action)
{
    const int index = static_cast<int>(g_scriptPaths.size());
    const int id = g_cmdBase + index;
    g_scriptPaths.push_back(path);
    g_scriptActions.push_back(action);
    g_executeOptions.emplace_back();
    const size_t tab = title.find(L'\t');
    g_scriptTitles.push_back(tab == std::wstring::npos ? title : title.substr(0, tab));
    g_cmdIds.push_back(id);
    if (!AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(id), title.c_str())) {
        g_scriptPaths.pop_back(); g_scriptActions.pop_back(); g_executeOptions.pop_back();
        g_scriptTitles.pop_back(); g_cmdIds.pop_back(); return false;
    }
    registerHotkeyForItem(title, id);
    return true;
}

static bool hasScriptExtension(const std::wstring& path)
{
    std::wstring lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    return (lower.size() >= 3 && lower.rfind(L".js") == lower.size() - 3) ||
           (lower.size() >= 4 && lower.rfind(L".mjs") == lower.size() - 4) ||
           (lower.size() >= 3 && lower.rfind(L".pl") == lower.size() - 3) ||
           (lower.size() >= 3 && lower.rfind(L".py") == lower.size() - 3);
}

static std::wstring scriptExtension(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return L"(none)";
    return path.substr(dot);
}

static HBITMAP createToolbarBitmap(HICON icon)
{
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);
    HDC screen = GetDC(nullptr);
    if (!screen) return nullptr;
    HBITMAP bitmap = CreateCompatibleBitmap(screen, cx, cy);
    HDC memory = bitmap ? CreateCompatibleDC(screen) : nullptr;
    if (bitmap && memory) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        RECT area{0, 0, cx, cy};
        HBRUSH background = CreateSolidBrush(RGB(255, 0, 255));
        FillRect(memory, &area, background);
        DeleteObject(background);
        DrawIconEx(memory, 0, 0, icon, cx, cy, 0, nullptr, DI_NORMAL);
        SelectObject(memory, old);
    } else if (bitmap) {
        DeleteObject(bitmap);
        bitmap = nullptr;
    }
    if (memory) DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return bitmap;
}

static bool loadToolbarIcon(const std::wstring& path, toolbarIconsWithDarkMode& handles)
{
    HICON icon = path.empty()
        ? static_cast<HICON>(LoadImageW(g_hModule, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0,
                                        LR_DEFAULTSIZE))
        : static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 0, 0,
                                        LR_LOADFROMFILE | LR_DEFAULTSIZE));
    if (!icon) return false;
    HICON darkIcon = CopyIcon(icon);
    HBITMAP bitmap = createToolbarBitmap(icon);
    if (!darkIcon || !bitmap) {
        DestroyIcon(icon);
        if (darkIcon) DestroyIcon(darkIcon);
        if (bitmap) DeleteObject(bitmap);
        return false;
    }
    handles.hToolbarBmp = bitmap;
    handles.hToolbarIcon = icon;
    handles.hToolbarIconDarkMode = darkIcon;
    return true;
}

static void runToolbarScript(size_t index)
{
    const size_t pathIndex = static_cast<size_t>(g_menuScriptCount) + index;
    if (pathIndex < g_scriptPaths.size())
        showScriptPath(g_cmdBase + static_cast<int>(pathIndex));
}

static void organizeToolbarCommandsInPluginMenu()
{
    HMENU pluginsMenu = reinterpret_cast<HMENU>(
        SendMessageW(g_nppData._nppHandle, NPPM_GETMENUHANDLE, NPPPLUGINMENU, 0));
    if (!pluginsMenu || g_toolbarScriptFiles.empty()) return;

    HMENU pluginMenu = nullptr;
    const int count = GetMenuItemCount(pluginsMenu);
    for (int i = 0; i < count; ++i) {
        wchar_t label[128] = {};
        GetMenuStringW(pluginsMenu, static_cast<UINT>(i), label,
                       static_cast<int>(std::size(label)), MF_BYPOSITION);
        std::wstring normalized = label;
        normalized.erase(std::remove(normalized.begin(), normalized.end(), L'&'), normalized.end());
        if (normalized == PLUGIN_NAME) {
            MENUITEMINFOW menuItem{};
            menuItem.cbSize = sizeof(menuItem);
            menuItem.fMask = MIIM_SUBMENU;
            if (GetMenuItemInfoW(pluginsMenu, static_cast<UINT>(i), TRUE, &menuItem))
                pluginMenu = menuItem.hSubMenu;
            break;
        }
    }
    if (!pluginMenu) return;

    const size_t baseCount = sizeof(g_funcItems) / sizeof(g_funcItems[0]);
    HMENU toolbarMenu = CreatePopupMenu();
    if (!toolbarMenu) return;
    for (size_t i = 0; i < g_toolbarScriptFiles.size(); ++i) {
        const UINT commandId = static_cast<UINT>(g_allFuncItems[baseCount + i]._cmdID);
        if (!AppendMenuW(toolbarMenu, MF_STRING, commandId, g_toolbarTitles[i].c_str())) {
            DestroyMenu(toolbarMenu);
            debugLog(L"Could not build NppMenuScripts Toolbar submenu");
            return;
        }
    }

    const UINT stopScriptCommandId = static_cast<UINT>(g_allFuncItems[4]._cmdID);
    int insertionPosition = GetMenuItemCount(pluginMenu);
    for (int i = 0; i < GetMenuItemCount(pluginMenu); ++i) {
        MENUITEMINFOW menuItem{};
        menuItem.cbSize = sizeof(menuItem);
        menuItem.fMask = MIIM_ID;
        if (GetMenuItemInfoW(pluginMenu, static_cast<UINT>(i), TRUE, &menuItem) &&
            menuItem.wID == stopScriptCommandId) {
            insertionPosition = i + 1;
            break;
        }
    }

    // Remove the flat plugin-menu entries before attaching their command IDs
    // below the new submenu. RemoveMenu may also find matching IDs in attached
    // submenus, so doing this after insertion can empty the Toolbar submenu.
    for (size_t i = 0; i < g_toolbarScriptFiles.size(); ++i) {
        const UINT commandId = static_cast<UINT>(g_allFuncItems[baseCount + i]._cmdID);
        RemoveMenu(pluginMenu, commandId, MF_BYCOMMAND);
    }

    if (!InsertMenuW(pluginMenu, static_cast<UINT>(insertionPosition),
                     MF_BYPOSITION | MF_POPUP,
                     reinterpret_cast<UINT_PTR>(toolbarMenu), tr("Toolbar").c_str())) {
        for (size_t i = 0; i < g_toolbarScriptFiles.size(); ++i) {
            const UINT commandId = static_cast<UINT>(g_allFuncItems[baseCount + i]._cmdID);
            AppendMenuW(pluginMenu, MF_STRING, commandId, g_toolbarTitles[i].c_str());
        }
        DestroyMenu(toolbarMenu);
        debugLog(L"Could not insert NppMenuScripts Toolbar submenu; restored toolbar commands at plugin-menu root");
        return;
    }
    DrawMenuBar(g_nppData._nppHandle);
}

template <size_t Index>
static void toolbarCommand()
{
    runToolbarScript(Index);
}

template <size_t... Indices>
static std::vector<void (*)()> makeToolbarCallbacks(std::index_sequence<Indices...>)
{
    return { &toolbarCommand<Indices>... };
}

static constexpr size_t MAX_TOOLBAR_ITEMS = 32;

static constexpr unsigned int MAX_MENU_NESTING = 16;

static bool parseCodePageValue(cJSON* value, UINT& codePage)
{
    if (cJSON_IsNumber(value)) {
        if (value->valuedouble < 1 || value->valuedouble > 65535 ||
            static_cast<double>(static_cast<UINT>(value->valuedouble)) != value->valuedouble)
            return false;
        codePage = static_cast<UINT>(value->valuedouble);
    } else if (cJSON_IsString(value) && value->valuestring) {
        std::wstring name = utf8ToWide(value->valuestring);
        name.erase(std::remove(name.begin(), name.end(), L'-'), name.end());
        std::transform(name.begin(), name.end(), name.begin(), ::towlower);
        if (name == L"ansi" || name == L"system") codePage = GetACP();
        else if (name == L"utf16" || name == L"utf16le") codePage = 1200;
        else if (name == L"utf16be") codePage = 1201;
        else if (name == L"utf32" || name == L"utf32le") codePage = 12000;
        else if (name == L"utf32be") codePage = 12001;
        else if (name == L"usascii") codePage = 20127;
        else if (name == L"iso88591") codePage = 28591;
        else if (name == L"utf7") codePage = 65000;
        else if (name == L"utf8") codePage = 65001;
        else {
            wchar_t* end = nullptr;
            const unsigned long parsed = std::wcstoul(name.c_str(), &end, 10);
            if (!end || end == name.c_str() || *end != L'\0' || parsed == 0 || parsed > 65535)
                return false;
            codePage = static_cast<UINT>(parsed);
        }
    } else {
        return false;
    }

    const bool manualUnicode = codePage == 1200 || codePage == 1201 ||
        codePage == 12000 || codePage == 12001;
    return manualUnicode || codePage == 65000 || IsValidCodePage(codePage) != FALSE;
}

static bool parseExecuteOptions(cJSON* item, ExecuteOptions& options)
{
    if (!cJSON_IsObject(item)) return false;
    cJSON* command = cJSON_GetObjectItemCaseSensitive(item, "command");
    if (!cJSON_IsString(command) || !command->valuestring || !command->valuestring[0]) return false;
    options.command = utf8ToWide(command->valuestring);

    cJSON* saveBeforeRun = cJSON_GetObjectItemCaseSensitive(item, "save_before_run");
    if (saveBeforeRun) {
        if (!cJSON_IsBool(saveBeforeRun)) return false;
        options.saveBeforeRun = cJSON_IsTrue(saveBeforeRun);
    }

    cJSON* codePage = cJSON_GetObjectItemCaseSensitive(item, "code_page");
    if (codePage && !parseCodePageValue(codePage, options.codePage)) return false;

    cJSON* runIn = cJSON_GetObjectItemCaseSensitive(item, "run_in");
    if (!cJSON_IsString(runIn)) return false;
    const std::wstring runInValue = utf8ToWide(runIn->valuestring);
    if (runInValue == L"output_pane") options.runIn = ExecuteRunIn::OutputPane;
    else if (runInValue == L"console") options.runIn = ExecuteRunIn::Console;
    else if (runInValue == L"windows") options.runIn = ExecuteRunIn::Windows;
    else return false;
    const bool requiresRawOutput = options.codePage == 1200 || options.codePage == 1201 ||
        options.codePage == 12000 || options.codePage == 12001 || options.codePage == 65000;
    if (requiresRawOutput && options.runIn == ExecuteRunIn::Console) return false;

    cJSON* environment = cJSON_GetObjectItemCaseSensitive(item, "env");
    if (environment) {
        if (!cJSON_IsObject(environment)) return false;
        for (cJSON* entry = environment->child; entry; entry = entry->next) {
            if (!entry->string || !entry->string[0] || std::wcschr(utf8ToWide(entry->string).c_str(), L'=') ||
                std::strlen(entry->string) > 32760)
                return false;
            if (!cJSON_IsString(entry) || !entry->valuestring) return false;
            options.environment[utf8ToWide(entry->string)] = utf8ToWide(entry->valuestring);
        }
    }
    return true;
}

static bool addJsonMenu(HMENU menu, cJSON* obj, const std::wstring& folder, unsigned int depth = 0)
{
    if (!cJSON_IsArray(obj) || depth > MAX_MENU_NESTING) return false;
    for (cJSON* item = obj->child; item; item = item->next) {
        if (!cJSON_IsObject(item)) return false;
        if (!item->child) {
            if (!AppendMenuW(menu, MF_SEPARATOR, 0, nullptr)) return false;
            continue;
        }
        cJSON* titleItem = cJSON_GetObjectItemCaseSensitive(item, "title");
        cJSON* scriptItem = cJSON_GetObjectItemCaseSensitive(item, "script");
        cJSON* actionItem = cJSON_GetObjectItemCaseSensitive(item, "action");
        cJSON* executeItem = cJSON_GetObjectItemCaseSensitive(item, "execute");
        cJSON* subItem = cJSON_GetObjectItemCaseSensitive(item, "sub");
        if (subItem) {
            if (!cJSON_IsString(titleItem) || !cJSON_IsArray(subItem) || scriptItem || actionItem || executeItem ||
                depth >= MAX_MENU_NESTING)
                return false;
            HMENU childMenu = CreatePopupMenu();
            if (!childMenu || !addJsonMenu(childMenu, subItem, folder, depth + 1)) {
                if (childMenu) DestroyMenu(childMenu);
                return false;
            }
            if (!AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(childMenu),
                             utf8ToWide(titleItem->valuestring).c_str())) {
                DestroyMenu(childMenu);
                return false;
            }
            continue;
        }
        if (!cJSON_IsString(titleItem) ||
            !hasExactlyOneExecutionTarget(scriptItem != nullptr, actionItem != nullptr, executeItem != nullptr)) return false;
        if (scriptItem) {
            if (!cJSON_IsString(scriptItem)) return false;
            const std::wstring script = utf8ToWide(scriptItem->valuestring);
            if (!hasScriptExtension(script) ||
                !addMenuItem(menu, utf8ToWide(titleItem->valuestring),
                             joinPath(folder, script), MenuCommandAction::RunScript)) return false;
        } else if (actionItem) {
            if (!cJSON_IsString(actionItem)) return false;
            MenuCommandAction action{};
            std::wstring target;
            if (!parseActionToken(utf8ToWide(actionItem->valuestring), action, target) ||
                !addMenuItem(menu, utf8ToWide(titleItem->valuestring), target, action)) return false;
        } else {
            ExecuteOptions options;
            if (!parseExecuteOptions(executeItem, options) ||
                !addMenuItem(menu, utf8ToWide(titleItem->valuestring), L"", MenuCommandAction::RunCommand))
                return false;
            g_executeOptions.back() = std::move(options);
        }
    }
    return true;
}

static bool validMenuItems(cJSON* items, unsigned int depth = 0)
{
    if (!cJSON_IsArray(items) || depth > MAX_MENU_NESTING) return false;
    for (cJSON* item = items->child; item; item = item->next) {
        if (!cJSON_IsObject(item)) return false;
        if (!item->child) continue; // Empty object is a separator.
        cJSON* title = cJSON_GetObjectItemCaseSensitive(item, "title");
        cJSON* script = cJSON_GetObjectItemCaseSensitive(item, "script");
        cJSON* actionItem = cJSON_GetObjectItemCaseSensitive(item, "action");
        cJSON* executeItem = cJSON_GetObjectItemCaseSensitive(item, "execute");
        cJSON* sub = cJSON_GetObjectItemCaseSensitive(item, "sub");
        if (sub) {
            if (!cJSON_IsString(title) || !cJSON_IsArray(sub) || script || actionItem || executeItem ||
                depth >= MAX_MENU_NESTING ||
                !validMenuItems(sub, depth + 1)) return false;
            continue;
        }
        if (!cJSON_IsString(title) ||
            !hasExactlyOneExecutionTarget(script != nullptr, actionItem != nullptr, executeItem != nullptr)) return false;
        if (script) {
            if (!cJSON_IsString(script) || !hasScriptExtension(utf8ToWide(script->valuestring))) return false;
        } else if (actionItem) {
            MenuCommandAction action{};
            std::wstring target;
            if (!cJSON_IsString(actionItem) ||
                !parseActionToken(utf8ToWide(actionItem->valuestring), action, target)) return false;
        } else {
            ExecuteOptions options;
            if (!parseExecuteOptions(executeItem, options)) return false;
        }
    }
    return true;
}

static int countMenuCommands(cJSON* items)
{
    int count = 0;
    for (cJSON* item = items ? items->child : nullptr; item; item = item->next) {
        cJSON* sub = cJSON_GetObjectItemCaseSensitive(item, "sub");
        if (sub) count += countMenuCommands(sub);
        else if (item->child) ++count;
    }
    return count;
}

static bool validMenuDefinition(cJSON* menu)
{
    if (!cJSON_IsObject(menu)) return false;
    for (cJSON* top = menu->child; top; top = top->next)
        if (!validMenuItems(top)) return false;
    return true;
}

static int findHelpMenuPosition(HMENU menu)
{
    if (!menu) return -1;

    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        wchar_t labelBuffer[256] = {};
        GetMenuStringW(menu, static_cast<UINT>(i), labelBuffer,
                       static_cast<int>(std::size(labelBuffer)), MF_BYPOSITION);
        std::wstring label = labelBuffer;
        label.erase(std::remove(label.begin(), label.end(), L'&'), label.end());
        const size_t first = label.find_first_not_of(L" \t");
        const size_t last = label.find_last_not_of(L" \t");
        label = first == std::wstring::npos ? L"" : label.substr(first, last - first + 1);
        std::wstring normalized = label;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::towlower);

        debugLog(L"Main menu item[" + std::to_wstring(i) + L"]='" + label + L"'");
        if (normalized == L"?" || normalized == L"help" || normalized == L"說明" ||
            normalized == L"帮助" || normalized == L"ヘルプ" || normalized == L"aide" ||
            normalized == L"hilfe") {
            debugLog(L"Help menu insertion position=" + std::to_wstring(i));
            return i;
        }
    }

    debugLog(L"Help menu was not found; script menus will be appended");
    return -1;
}

static void removeMenus()
{
    unregisterHotkeys();
    if (!g_mainMenu) return;
    const int count = GetMenuItemCount(g_mainMenu);
    for (int i = count - 1; i >= 0; --i) {
        MENUITEMINFOW mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_SUBMENU;
        if (GetMenuItemInfoW(g_mainMenu, static_cast<UINT>(i), TRUE, &mi)) {
            for (HMENU h : g_topMenus) {
                if (mi.hSubMenu == h) { RemoveMenu(g_mainMenu, static_cast<UINT>(i), MF_BYPOSITION); break; }
            }
        }
    }
    for (HMENU h : g_topMenus) DestroyMenu(h);
    DrawMenuBar(g_nppData._nppHandle);
    g_topMenus.clear(); g_mainMenu = nullptr;
    g_scriptPaths.clear(); g_scriptActions.clear(); g_scriptTitles.clear(); g_executeOptions.clear();
    g_cmdIds.clear(); g_cmdBase = 0; g_cmdCount = 0; g_installed = false;
    destroyHotkeyWindow();
}

static bool installMenus(const std::wstring& jsonPath, bool forceRebuild)
{
    if (g_installed && !forceRebuild) return true;
    const std::wstring jsonText = readUtf8File(jsonPath);
    if (jsonText.empty()) {
        const std::wstring message = trFormat("MenuJsonEmpty", {} ) + L"\r\n\r\n" + jsonPath;
        MessageBoxW(g_nppData._nppHandle, message.c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR);
        return false;
    }
    std::ifstream f(jsonPath.c_str(), std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) bytes.erase(0, 3);
    cJSON* root = cJSON_Parse(bytes.c_str());
    if (!root) {
        const char* err = cJSON_GetErrorPtr();
        std::wstring msg = trFormat("JsonParseError", {{L"{error}", utf8ToWide(err ? err : "unknown error")}});
        MessageBoxW(g_nppData._nppHandle, msg.c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR); return false;
    }
    cJSON* scriptFolderItem = cJSON_GetObjectItemCaseSensitive(root, "script_folder");
    cJSON* debugItem = cJSON_GetObjectItemCaseSensitive(root, "debug");
    cJSON* menuItem = cJSON_GetObjectItemCaseSensitive(root, "menu");
    cJSON* iconFolderItem = cJSON_GetObjectItemCaseSensitive(root, "icon_folder");
    cJSON* toolbarItem = cJSON_GetObjectItemCaseSensitive(root, "toolbar");
    if (!cJSON_IsString(scriptFolderItem) ||
        !validMenuDefinition(menuItem) || (debugItem && !cJSON_IsBool(debugItem)) ||
        (iconFolderItem && !cJSON_IsString(iconFolderItem)) ||
        (toolbarItem && !cJSON_IsArray(toolbarItem))) {
        cJSON_Delete(root);
        MessageBoxW(g_nppData._nppHandle, tr("InvalidMenuJson").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR);
        return false;
    }
    // Validate the new configuration completely before removing the current menu.
    // This keeps the existing menu available when Rebuild Menu & Toolbar encounters invalid JSON.
    int itemCount = 0;
    for (cJSON* top = menuItem->child; top; top = top->next)
        itemCount += countMenuCommands(top);
    const std::wstring configuredIconFolder = iconFolderItem
        ? utf8ToWide(iconFolderItem->valuestring) : L"./icon";
    const std::wstring resolvedIconFolder = resolveScriptFolder(jsonPath, configuredIconFolder);
    std::vector<std::wstring> configuredToolbarScripts;
    std::vector<MenuCommandAction> configuredToolbarActions;
    std::vector<std::wstring> configuredToolbarActionTargets;
    std::vector<ExecuteOptions> configuredToolbarExecuteOptions;
    std::vector<std::wstring> configuredToolbarIcons;
    std::vector<std::wstring> configuredToolbarTitles;
    if (toolbarItem) {
        for (cJSON* item = toolbarItem->child; item; item = item->next) {
            cJSON* iconItem = cJSON_GetObjectItemCaseSensitive(item, "icon");
            cJSON* scriptItem = cJSON_GetObjectItemCaseSensitive(item, "script");
            cJSON* actionItem = cJSON_GetObjectItemCaseSensitive(item, "action");
            cJSON* executeItem = cJSON_GetObjectItemCaseSensitive(item, "execute");
            cJSON* titleItem = cJSON_GetObjectItemCaseSensitive(item, "title");
            if (!cJSON_IsObject(item) || (iconItem && !cJSON_IsString(iconItem)) ||
                !cJSON_IsString(titleItem) ||
                !hasExactlyOneExecutionTarget(scriptItem != nullptr, actionItem != nullptr,
                                              executeItem != nullptr)) {
                cJSON_Delete(root);
                MessageBoxW(g_nppData._nppHandle, tr("InvalidToolbarEntry").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR);
                return false;
            }
            MenuCommandAction action = MenuCommandAction::RunScript;
            std::wstring script;
            std::wstring actionTarget;
            if (scriptItem) {
                if (!cJSON_IsString(scriptItem)) {
                    cJSON_Delete(root);
                    MessageBoxW(g_nppData._nppHandle, tr("InvalidToolbarEntry").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR);
                    return false;
                }
                script = utf8ToWide(scriptItem->valuestring);
                if (!hasScriptExtension(script)) {
                    cJSON_Delete(root);
                    MessageBoxW(g_nppData._nppHandle, tr("InvalidToolbarEntry").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR);
                    return false;
                }
                configuredToolbarExecuteOptions.emplace_back();
            } else if (actionItem && (!cJSON_IsString(actionItem) ||
                       !parseActionToken(utf8ToWide(actionItem->valuestring), action, actionTarget))) {
                cJSON_Delete(root);
                MessageBoxW(g_nppData._nppHandle, tr("InvalidToolbarEntry").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR);
                return false;
            } else if (executeItem) {
                ExecuteOptions options;
                if (!parseExecuteOptions(executeItem, options)) {
                    cJSON_Delete(root);
                    MessageBoxW(g_nppData._nppHandle, tr("InvalidToolbarEntry").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR);
                    return false;
                }
                action = MenuCommandAction::RunCommand;
                configuredToolbarExecuteOptions.push_back(std::move(options));
            } else {
                configuredToolbarExecuteOptions.emplace_back();
            }
            std::wstring iconPath;
            if (iconItem) {
                const std::wstring iconName = utf8ToWide(iconItem->valuestring);
                std::wstring lowerIcon = iconName;
                std::transform(lowerIcon.begin(), lowerIcon.end(), lowerIcon.begin(), ::towlower);
                if (lowerIcon.size() < 4 || lowerIcon.rfind(L".ico") != lowerIcon.size() - 4) {
                    cJSON_Delete(root);
                    MessageBoxW(g_nppData._nppHandle, tr("InvalidToolbarIcon").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR);
                    return false;
                }
                iconPath = joinPath(resolvedIconFolder, iconName);
            }
            configuredToolbarScripts.push_back(script);
            configuredToolbarActions.push_back(action);
            configuredToolbarActionTargets.push_back(std::move(actionTarget));
            configuredToolbarIcons.push_back(std::move(iconPath));
            configuredToolbarTitles.push_back(utf8ToWide(titleItem->valuestring));
        }
    }
    if (configuredToolbarScripts.size() > MAX_TOOLBAR_ITEMS) {
        cJSON_Delete(root);
        MessageBoxW(g_nppData._nppHandle, tr("ToolbarLimit").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR);
        return false;
    }
    if (g_toolbarIconsRegistered) {
        if (configuredToolbarScripts != g_toolbarScriptFiles ||
            configuredToolbarActions != g_toolbarActions ||
            configuredToolbarActionTargets != g_toolbarActionTargets ||
            configuredToolbarExecuteOptions != g_toolbarExecuteOptions ||
            configuredToolbarIcons != g_toolbarIconFiles) {
            debugLog(L"Toolbar configuration changed; restart Notepad++ for toolbar changes to take effect");
        }
    } else {
        g_toolbarScriptFiles = configuredToolbarScripts;
        g_toolbarActions = configuredToolbarActions;
        g_toolbarActionTargets = configuredToolbarActionTargets;
        g_toolbarExecuteOptions = configuredToolbarExecuteOptions;
        g_toolbarIconFiles = configuredToolbarIcons;
        g_toolbarTitles = configuredToolbarTitles;
        g_toolbarIcons.resize(g_toolbarScriptFiles.size());
    }
    itemCount += static_cast<int>(g_toolbarScriptFiles.size());
    if (forceRebuild && g_installed) {
        removeMenus();
    }
    if (itemCount > 0) {
        if (!SendMessageW(g_nppData._nppHandle, NPPM_ALLOCATECMDID, static_cast<WPARAM>(itemCount), reinterpret_cast<LPARAM>(&g_cmdBase))) {
            cJSON_Delete(root);
            MessageBoxW(g_nppData._nppHandle, tr("AllocateMenuIdsFailed").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR); return false;
        }
        g_cmdCount = itemCount;
    }
    if (!createHotkeyWindow()) { cJSON_Delete(root); MessageBoxW(g_nppData._nppHandle, tr("HotkeyWindowFailed").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR); return false; }
    g_outputDefaultTitle = tr("OutputPane");
    setOutputPaneTitle(g_outputDefaultTitle);
    g_mainMenu = reinterpret_cast<HMENU>(SendMessageW(g_nppData._nppHandle, NPPM_GETMENUHANDLE, NPPMAINMENU, 0));
    if (!g_mainMenu) { cJSON_Delete(root); MessageBoxW(g_nppData._nppHandle, tr("MainMenuFailed").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR); return false; }
    const std::wstring configuredFolder = utf8ToWide(scriptFolderItem->valuestring);
    const std::wstring folder = resolveScriptFolder(jsonPath, configuredFolder);
    g_menuJsonPath = fullPath(jsonPath);
    g_scriptFolder = folder;
    g_iconFolder = resolvedIconFolder;
    // An explicit menu.json boolean overrides the build-time default.
    // When omitted, NPPMENUSCRIPTS_DEBUG_DEFAULT determines whether logging is enabled.
    g_debugEnabled = debugItem ? cJSON_IsTrue(debugItem) : (NPPMENUSCRIPTS_DEBUG_DEFAULT != 0);
    g_debugLogPath = g_debugEnabled ? joinPath(g_scriptFolder, L"NppMenuScripts-debug.log") : L"";
    debugLog(L"=== NppMenuScripts debug session ===");
    debugLog(L"menu.json=" + jsonPath);
    debugLog(L"script_folder=" + g_scriptFolder);
    debugLog(L"output_pane_title=" + g_outputTitle);
    int helpMenuPosition = findHelpMenuPosition(g_mainMenu);
    for (cJSON* top = menuItem->child; top; top = top->next) {
        if (!top->string || !cJSON_IsArray(top)) continue;
        HMENU sub = CreatePopupMenu();
        if (!sub || !addJsonMenu(sub, top, folder)) {
            if (sub) DestroyMenu(sub); cJSON_Delete(root); removeMenus(); return false;
        }
        const std::wstring title = utf8ToWide(top->string);
        const BOOL inserted = helpMenuPosition >= 0
            ? InsertMenuW(g_mainMenu, static_cast<UINT>(helpMenuPosition),
                          MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(sub), title.c_str())
            : AppendMenuW(g_mainMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), title.c_str());
        if (!inserted) {
            const DWORD insertError = GetLastError();
            debugLog(L"Script menu insertion failed title='" + title + L"' error=" + std::to_wstring(insertError));
            if (helpMenuPosition >= 0 && AppendMenuW(g_mainMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), title.c_str())) {
                debugLog(L"Script menu appended as fallback title='" + title + L"'");
            } else {
                DestroyMenu(sub); cJSON_Delete(root); removeMenus(); return false;
            }
        } else if (helpMenuPosition >= 0) {
            debugLog(L"Script menu inserted before Help title='" + title + L"' position=" + std::to_wstring(helpMenuPosition));
            ++helpMenuPosition;
        }
        g_topMenus.push_back(sub);
    }
    g_menuScriptCount = static_cast<int>(g_scriptPaths.size());
    for (size_t i = 0; i < g_toolbarScriptFiles.size(); ++i) {
        const std::wstring& script = g_toolbarScriptFiles[i];
        const MenuCommandAction action = g_toolbarActions[i];
        g_scriptPaths.push_back(action == MenuCommandAction::RunScript ? joinPath(folder, script) :
                                action == MenuCommandAction::RunMenuCommand ? g_toolbarActionTargets[i] : L"");
        g_scriptActions.push_back(action);
        g_scriptTitles.push_back(g_toolbarTitles[i]);
        g_executeOptions.push_back(g_toolbarExecuteOptions[i]);
        g_cmdIds.push_back(g_cmdBase + static_cast<int>(g_scriptPaths.size()) - 1);
    }
    DrawMenuBar(g_nppData._nppHandle);
    g_installed = true;
    debugLog(L"installMenus complete scriptCount=" + std::to_wstring(g_scriptPaths.size()));
    cJSON_Delete(root);
    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { g_hModule = hModule; DisableThreadLibraryCalls(hModule); }
    return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData nppData)
{
    g_nppData = nppData;
    g_workspaceFolder = loadConfiguredWorkspacePath();
    if (g_workspaceFolder.empty() || isPathWithinDirectory(moduleDirectory(), g_workspaceFolder))
        g_workspaceFolder = defaultWorkspaceDirectory();
    if (!initializeWorkspaceContents(g_workspaceFolder)) {
        MessageBoxW(g_nppData._nppHandle, tr("WorkspaceInitFailed").c_str(), L"NppMenuScripts", MB_OK | MB_ICONERROR);
        return;
    }
    installMenus(workspaceMenuPath());
}

extern "C" __declspec(dllexport) const wchar_t* getName() { return PLUGIN_NAME; }

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* nbF)
{
    swprintf_s(g_funcItems[6]._itemName, L"About NppMenuScripts (v%u.%u.%u.%u)",
               NPP_MENUSCRIPTS_VERSION_MAJOR, NPP_MENUSCRIPTS_VERSION_MINOR,
               NPP_MENUSCRIPTS_VERSION_PATCH, NPP_MENUSCRIPTS_VERSION_BUILD);
    if (g_allFuncItems.empty()) {
        g_allFuncItems.assign(std::begin(g_funcItems), std::end(g_funcItems));
        const auto callbacks = makeToolbarCallbacks(std::make_index_sequence<MAX_TOOLBAR_ITEMS>{});
        for (size_t i = 0; i < g_toolbarScriptFiles.size(); ++i) {
            FuncItem item{};
            wcsncpy_s(item._itemName, g_toolbarTitles[i].c_str(), _TRUNCATE);
            item._pFunc = callbacks[i];
            g_allFuncItems.push_back(item);
        }
    }
    *nbF = static_cast<int>(g_allFuncItems.size());
    return g_allFuncItems.data();
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notifyCode)
{
    if (notifyCode && notifyCode->nmhdr.code == NPPN_READY) {
        loadTranslations();
        // setInfo() can run before Notepad++ has finished restoring its UI and
        // session. Reinstall the configured menus once the main window is ready
        // so startup-time menu changes cannot leave stale or misplaced entries.
        if (g_installed && !g_menuJsonPath.empty()) {
            const std::wstring menuPath = g_menuJsonPath;
            debugLog(L"NPPN_READY received; rebuilding menus after Notepad++ initialization");
            if (!installMenus(menuPath, true)) {
                debugLog(L"NPPN_READY menu rebuild failed; retrying installation");
                installMenus(menuPath, false);
            }
        }
        g_outputDefaultTitle = tr("OutputPane");
        if (g_outputDefaultTitle.empty()) g_outputDefaultTitle = L"Output Pane";
        setOutputPaneTitle(g_outputDefaultTitle);
        installPluginMenuMessageHook();
        applyTranslatedPluginMenu();
        return;
    }
    if (notifyCode && notifyCode->nmhdr.code == NPPN_NATIVELANGCHANGED) {
        debugLog(L"NPPN_NATIVELANGCHANGED received; scheduling translation refresh");
        if (g_hotkeyWnd) {
            PostMessageW(g_hotkeyWnd, WM_NPPMENUSCRIPTS_REFRESH_LANGUAGE, 0, 0);
        } else {
            loadTranslations();
            g_outputDefaultTitle = tr("OutputPane");
            if (g_outputDefaultTitle.empty()) g_outputDefaultTitle = L"Output Pane";
            setOutputPaneTitle(g_outputDefaultTitle);
            applyTranslatedPluginMenu();
        }
        return;
    }
    if (notifyCode && notifyCode->nmhdr.code == NPPN_TBMODIFICATION && !g_toolbarIconsRegistered) {
        const size_t baseCount = sizeof(g_funcItems) / sizeof(g_funcItems[0]);
        for (size_t i = 0; i < g_toolbarScriptFiles.size(); ++i) {
            if (!loadToolbarIcon(g_toolbarIconFiles[i], g_toolbarIcons[i])) {
                debugLog(L"Toolbar icon could not be loaded: " + g_toolbarIconFiles[i]);
                continue;
            }
            const int commandId = g_allFuncItems[baseCount + i]._cmdID;
            if (!SendMessageW(g_nppData._nppHandle, NPPM_ADDTOOLBARICON_FORDARKMODE,
                              static_cast<WPARAM>(commandId),
                              reinterpret_cast<LPARAM>(&g_toolbarIcons[i]))) {
                debugLog(L"Toolbar icon registration failed for command id=" + std::to_wstring(commandId));
            } else {
                debugLog(L"Toolbar icon registered for command id=" + std::to_wstring(commandId));
            }
        }
        organizeToolbarCommandsInPluginMenu();
        g_toolbarIconsRegistered = true;
        return;
    }
    if (notifyCode && notifyCode->nmhdr.code == NPPN_SHUTDOWN) {
        removePluginMenuMessageHook();
        g_shuttingDown.store(true);
        g_stopRequested.store(true);
        {
            std::lock_guard<std::mutex> lock(g_processHandleMutex);
            if (g_processJob) {
                if (!TerminateJobObject(g_processJob, ERROR_CANCELLED) && g_processHandle)
                    TerminateProcess(g_processHandle, ERROR_CANCELLED);
            } else if (g_processHandle) {
                TerminateProcess(g_processHandle, 1);
            }
        }
        if (g_processThread.joinable()) {
            g_processThread.join();
        }
        removeMenus();
        destroyOutputPane();
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT Message, WPARAM wParam, LPARAM)
{
    if (Message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        if (id >= g_cmdBase && id < g_cmdBase + g_cmdCount) { showScriptPath(id); return TRUE; }
    }
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
