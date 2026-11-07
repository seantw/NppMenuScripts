#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <wrl.h>
#include <WebView2.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "Docking.h"
#include "HtmlPane.h"
#include "MenuConfigEditorResource.h"
#include "Notepad_plus_msgs.h"

using Microsoft::WRL::ComPtr;

namespace {
constexpr int HTML_DLG_ID = 0x4E4B;
constexpr int HTML_NAV_HOME_BUTTON_ID = 0x4E4E;
constexpr int HTML_NAV_BACK_BUTTON_ID = 0x4E4C;
constexpr int HTML_NAV_RELOAD_BUTTON_ID = 0x4E4D;
constexpr int HTML_NAV_TOOLBAR_HEIGHT = 21;
constexpr int HTML_NAV_BUTTON_SIZE = 16;
constexpr wchar_t HTML_CLASS[] = L"NppMenuScripts_WebViewPane";
// Use a reserved example TLD for the virtual host. Microsoft recommends
// avoiding .local because it can trigger local-network name resolution.
constexpr wchar_t HTML_HOST[] = L"nppmenuscripts.example";
using CreateEnvironmentFn = HRESULT (STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

template<class Interface, class... Args>
class LambdaEventHandler final : public Interface {
public:
    LambdaEventHandler(REFIID interfaceId, std::function<HRESULT(Args...)> callback)
        : interfaceId_(interfaceId), callback_(std::move(callback)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, interfaceId_)) {
            *object = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return callback_(args...); }

private:
    IID interfaceId_{};
    std::atomic<ULONG> references_{1};
    std::function<HRESULT(Args...)> callback_;
};

template<class Interface, class... Args, class Fn>
ComPtr<Interface> makeHandler(REFIID interfaceId, Fn&& callback)
{
    ComPtr<Interface> handler;
    handler.Attach(new LambdaEventHandler<Interface, Args...>(
        interfaceId, std::function<HRESULT(Args...)>(std::forward<Fn>(callback))));
    return handler;
}

HWND g_npp = nullptr;
HWND g_pane = nullptr;
HINSTANCE g_module = nullptr;
HMODULE g_loader = nullptr;
DockedWidgetData g_dock{};
ComPtr<ICoreWebView2Environment> g_environment;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;
std::wstring g_htmlPath;
std::wstring g_htmlFolder;
std::wstring g_homeUrl;
std::wstring g_paneTitle;
bool g_advancedPermissions = false;
HtmlWebMessageHandler g_messageHandler;
bool g_bridgeInjected = false;
bool g_comInitialized = false;
HWND g_navToolbar = nullptr;
HICON g_navHomeIcon = nullptr;
HICON g_navBackIcon = nullptr;
HICON g_navReloadIcon = nullptr;
HIMAGELIST g_navImageList = nullptr;
bool g_showNavToolbar = false;

constexpr wchar_t HTML_BRIDGE_SCRIPT[] = LR"JS((() => {
  if (location.origin !== 'https://nppmenuscripts.example' || !window.chrome?.webview) return;
  const pending = new Map();
  let nextId = 1;
  window.chrome.webview.addEventListener('message', event => {
    let response;
    try { response = JSON.parse(event.data); } catch (_) { return; }
    const entry = pending.get(response.id);
    if (!entry) return;
    pending.delete(response.id);
    if (response.ok) entry.resolve(response.value);
    else entry.reject(new Error(response.error || 'NppMenuScripts API call failed'));
  });
  const call = (method, value) => new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    window.chrome.webview.postMessage(JSON.stringify({ id, method, value }));
  });
  Object.defineProperty(window, 'npp', { configurable: false, writable: false, value: Object.freeze({
    getText: () => call('getText'),
    setText: text => call('setText', String(text)),
    getSelection: () => call('getSelection'),
    setSelection: text => call('setSelection', String(text)),
    getClipBoard: () => call('getClipBoard'),
    setClipBoard: text => call('setClipBoard', String(text))
  }) });
})();)JS";

std::wstring moduleFolder()
{
    std::vector<wchar_t> path(32768);
    DWORD size = GetModuleFileNameW(g_module, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) return L".";
    std::wstring value(path.data(), size);
    const size_t slash = value.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : value.substr(0, slash);
}

std::wstring canonicalPath(const std::wstring& path)
{
    if (path.empty()) return {};
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()),
                                          buffer.data(), nullptr);
    if (!length || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

std::wstring userDataFolder()
{
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)) || !local)
        return L".";
    std::wstring path = local;
    CoTaskMemFree(local);
    path += L"\\NppMenuScripts\\WebView2";
    SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return path;
}

std::wstring htmlUrl(const std::wstring& path)
{
    std::wstring filename = path.substr(path.find_last_of(L"\\/") == std::wstring::npos ? 0 : path.find_last_of(L"\\/") + 1);
    std::vector<wchar_t> encoded(filename.size() * 3 + 16);
    DWORD capacity = static_cast<DWORD>(encoded.size());
    const std::wstring prefix = std::wstring(L"https://") + HTML_HOST + L"/";
    if (FAILED(UrlEscapeW(filename.c_str(), encoded.data(), &capacity,
                          URL_ESCAPE_SEGMENT_ONLY | URL_ESCAPE_PERCENT)))
        return prefix;
    return prefix + std::wstring(encoded.data());
}

bool localOrigin(PCWSTR uri)
{
    const std::wstring prefix = std::wstring(L"https://") + HTML_HOST + L"/";
    return uri && _wcsnicmp(uri, prefix.c_str(), prefix.size()) == 0;
}

void showHtmlFailure(const wchar_t* operation, HRESULT result)
{
    wchar_t message[256]{};
    swprintf_s(message, L"%s failed (HRESULT 0x%08X).", operation,
               static_cast<unsigned int>(result));
    if (g_npp) MessageBoxW(g_npp, message, L"NppMenuScripts HTML", MB_OK | MB_ICONERROR);
}

void setBounds()
{
    if (!g_controller || !g_pane) return;
    RECT rc{};
    GetClientRect(g_pane, &rc);
    if (g_navToolbar) {
        const int width = rc.right - rc.left;
        SetWindowPos(g_navToolbar, HWND_TOP, 0, 0, width, HTML_NAV_TOOLBAR_HEIGHT,
                     SWP_NOACTIVATE | (g_showNavToolbar ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
    if (g_showNavToolbar) rc.top += HTML_NAV_TOOLBAR_HEIGHT;
    g_controller->put_Bounds(rc);
}

void updateNavigationToolbar(bool show)
{
    if (g_showNavToolbar == show) return;
    g_showNavToolbar = show;
    if (g_pane && g_controller) {
        setBounds();
        if (g_navToolbar && show) SetWindowPos(g_navToolbar, HWND_TOP, 0, 0, 0, 0,
                                               SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(g_pane, nullptr, TRUE);
        UpdateWindow(g_pane);
    }
}

void updateBackButtonState()
{
    if (!g_navToolbar || !g_webview) return;
    BOOL canGoBack = FALSE;
    if (SUCCEEDED(g_webview->get_CanGoBack(&canGoBack)))
        SendMessageW(g_navToolbar, TB_ENABLEBUTTON, HTML_NAV_BACK_BUTTON_ID,
                     MAKELONG(canGoBack != FALSE, 0));
}

void createNavigationToolbar()
{
    if (!g_pane || g_navToolbar) return;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    g_navToolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"",
        WS_CHILD | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_CUSTOMERASE |
            CCS_NORESIZE | CCS_NOPARENTALIGN | CCS_NODIVIDER,
        0, 0, 0, HTML_NAV_TOOLBAR_HEIGHT, g_pane, nullptr, g_module, nullptr);
    if (!g_navToolbar) return;
    SendMessageW(g_navToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(g_navToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(HTML_NAV_BUTTON_SIZE, HTML_NAV_BUTTON_SIZE));
    SendMessageW(g_navToolbar, TB_SETMAXTEXTROWS, 0, 0);
    g_navHomeIcon = static_cast<HICON>(LoadImageW(g_module, MAKEINTRESOURCEW(IDR_HTML_HOME_ICON),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    g_navBackIcon = static_cast<HICON>(LoadImageW(g_module, MAKEINTRESOURCEW(IDR_HTML_BACK_ICON),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    g_navReloadIcon = static_cast<HICON>(LoadImageW(g_module, MAKEINTRESOURCEW(IDR_HTML_RELOAD_ICON),
        IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    g_navImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 3, 0);
    if (g_navImageList && g_navHomeIcon && g_navBackIcon && g_navReloadIcon) {
        ImageList_AddIcon(g_navImageList, g_navHomeIcon);
        ImageList_AddIcon(g_navImageList, g_navBackIcon);
        ImageList_AddIcon(g_navImageList, g_navReloadIcon);
        SendMessageW(g_navToolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(g_navImageList));
    }
    const int stringBase = static_cast<int>(SendMessageW(g_navToolbar, TB_ADDSTRINGW, 0,
        reinterpret_cast<LPARAM>(L"Home\0Back\0Reload\0")));
    TBBUTTON buttons[3]{};
    buttons[0].iBitmap = 0;
    buttons[0].idCommand = HTML_NAV_HOME_BUTTON_ID;
    buttons[0].fsState = TBSTATE_ENABLED;
    buttons[0].fsStyle = BTNS_BUTTON;
    buttons[0].iString = stringBase;
    buttons[1].iBitmap = 1;
    buttons[1].idCommand = HTML_NAV_BACK_BUTTON_ID;
    buttons[1].fsState = TBSTATE_ENABLED;
    buttons[1].fsStyle = BTNS_BUTTON;
    buttons[1].iString = stringBase + 1;
    buttons[2].iBitmap = 2;
    buttons[2].idCommand = HTML_NAV_RELOAD_BUTTON_ID;
    buttons[2].fsState = TBSTATE_ENABLED;
    buttons[2].fsStyle = BTNS_BUTTON;
    buttons[2].iString = stringBase + 2;
    SendMessageW(g_navToolbar, TB_ADDBUTTONS, 3, reinterpret_cast<LPARAM>(buttons));
    SendMessageW(g_navToolbar, TB_AUTOSIZE, 0, 0);
    ShowWindow(g_navToolbar, SW_HIDE);
}

void initializeWebView()
{
    if (!g_pane || g_htmlPath.empty()) return;
    if (!g_loader) {
        g_loader = LoadLibraryW((moduleFolder() + L"\\WebView2Loader.dll").c_str());
    }
    auto createEnvironment = g_loader ? reinterpret_cast<CreateEnvironmentFn>(
        GetProcAddress(g_loader, "CreateCoreWebView2EnvironmentWithOptions")) : nullptr;
    if (!createEnvironment) return;
    if (!g_comInitialized) {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        g_comInitialized = SUCCEEDED(hr);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return;
    }
    const std::wstring dataPath = userDataFolder();
    auto environmentHandler = makeHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
        HRESULT, ICoreWebView2Environment*>(
            IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
            [](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(result) || !environment || !g_pane) {
                    if (FAILED(result)) showHtmlFailure(L"WebView2 environment creation", result);
                    return result;
                }
                g_environment = environment;
                auto controllerHandler = makeHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                    HRESULT, ICoreWebView2Controller*>(
                        IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                        [](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controllerResult) || !controller || !g_pane) {
                                if (FAILED(controllerResult)) showHtmlFailure(L"WebView2 controller creation", controllerResult);
                                return controllerResult;
                            }
                            g_controller = controller;
                            HRESULT hr = g_controller->get_CoreWebView2(&g_webview);
                            if (FAILED(hr) || !g_webview) {
                                showHtmlFailure(L"Getting the WebView2 instance", FAILED(hr) ? hr : E_FAIL);
                                return FAILED(hr) ? hr : E_FAIL;
                            }
                            setBounds();
                            updateBackButtonState();
                            ComPtr<ICoreWebView2_3> webview3;
                            hr = g_webview->QueryInterface(IID_ICoreWebView2_3,
                                    reinterpret_cast<void**>(webview3.GetAddressOf()));
                            if (FAILED(hr) || !webview3) {
                                showHtmlFailure(L"Querying WebView2 local-content support", FAILED(hr) ? hr : E_NOINTERFACE);
                                return FAILED(hr) ? hr : E_NOINTERFACE;
                            }
                            hr = webview3->SetVirtualHostNameToFolderMapping(
                                HTML_HOST, g_htmlFolder.c_str(), g_advancedPermissions
                                    ? COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                                    : COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                            if (FAILED(hr)) {
                                showHtmlFailure(L"Mapping the local HTML folder", hr);
                                return hr;
                            }
                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
                                settings->put_IsScriptEnabled(TRUE);
                                settings->put_AreHostObjectsAllowed(FALSE);
                                settings->put_IsWebMessageEnabled(g_advancedPermissions ? TRUE : FALSE);
                            }
                            if (g_advancedPermissions) {
                                ComPtr<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler> scriptHandler =
                                    makeHandler<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler,
                                        HRESULT, LPCWSTR>(IID_ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler,
                                        [](HRESULT result, LPCWSTR) -> HRESULT {
                                            if (SUCCEEDED(result)) g_bridgeInjected = true;
                                            return S_OK;
                                        });
                                g_webview->AddScriptToExecuteOnDocumentCreated(HTML_BRIDGE_SCRIPT, scriptHandler.Get());
                            }
                            g_webview->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                            EventRegistrationToken resourceToken{};
                            auto resourceHandler = makeHandler<ICoreWebView2WebResourceRequestedEventHandler,
                                ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*>(
                                    IID_ICoreWebView2WebResourceRequestedEventHandler,
                                    [](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                                        ComPtr<ICoreWebView2WebResourceRequest> request;
                                        LPWSTR uri = nullptr;
                                        bool local = false;
                                        if (SUCCEEDED(args->get_Request(&request)) && request &&
                                            SUCCEEDED(request->get_Uri(&uri))) {
                                            local = localOrigin(uri);
                                            CoTaskMemFree(uri);
                                        }
                                        if (local || g_advancedPermissions) return S_OK;
                                        IStream* empty = SHCreateMemStream(nullptr, 0);
                                        if (!empty) return E_OUTOFMEMORY;
                                        ComPtr<ICoreWebView2WebResourceResponse> response;
                                        HRESULT hr = g_environment->CreateWebResourceResponse(
                                            empty, 403, L"Forbidden", L"Content-Length: 0", &response);
                                        empty->Release();
                                        if (FAILED(hr)) return hr;
                                        return args->put_Response(response.Get());
                                    });
                            g_webview->add_WebResourceRequested(resourceHandler.Get(), &resourceToken);
                            EventRegistrationToken navigationToken{};
                            auto navigationHandler = makeHandler<ICoreWebView2NavigationStartingEventHandler,
                                ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
                                    IID_ICoreWebView2NavigationStartingEventHandler,
                                    [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                        LPWSTR uri = nullptr;
                                        if (SUCCEEDED(args->get_Uri(&uri))) {
                                            const bool permitted = g_advancedPermissions || localOrigin(uri);
                                            if (!permitted) args->put_Cancel(TRUE);
                                            CoTaskMemFree(uri);
                                        } else if (!g_advancedPermissions) args->put_Cancel(TRUE);
                                        return S_OK;
                                    });
                            g_webview->add_NavigationStarting(navigationHandler.Get(), &navigationToken);
                            EventRegistrationToken navigationCompletedToken{};
                            auto navigationCompletedHandler = makeHandler<ICoreWebView2NavigationCompletedEventHandler,
                                ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
                                    IID_ICoreWebView2NavigationCompletedEventHandler,
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                        LPWSTR uri = nullptr;
                                        if (SUCCEEDED(sender->get_Source(&uri))) {
                                            updateNavigationToolbar(!localOrigin(uri));
                                            CoTaskMemFree(uri);
                                        }
                                        updateBackButtonState();
                                        return S_OK;
                                    });
                            g_webview->add_NavigationCompleted(navigationCompletedHandler.Get(), &navigationCompletedToken);
                            EventRegistrationToken newWindowToken{};
                            auto newWindowHandler = makeHandler<ICoreWebView2NewWindowRequestedEventHandler,
                                ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
                                    IID_ICoreWebView2NewWindowRequestedEventHandler,
                                    [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                        return args->put_Handled(g_advancedPermissions ? FALSE : TRUE);
                                    });
                            g_webview->add_NewWindowRequested(newWindowHandler.Get(), &newWindowToken);
                            EventRegistrationToken permissionToken{};
                            auto permissionHandler = makeHandler<ICoreWebView2PermissionRequestedEventHandler,
                                ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs*>(
                                    IID_ICoreWebView2PermissionRequestedEventHandler,
                                    [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                                        return args->put_State(g_advancedPermissions
                                            ? COREWEBVIEW2_PERMISSION_STATE_DEFAULT
                                            : COREWEBVIEW2_PERMISSION_STATE_DENY);
                                    });
                            g_webview->add_PermissionRequested(permissionHandler.Get(), &permissionToken);
                            EventRegistrationToken webMessageToken{};
                            auto webMessageHandler = makeHandler<ICoreWebView2WebMessageReceivedEventHandler,
                                ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
                                    IID_ICoreWebView2WebMessageReceivedEventHandler,
                                    [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        if (!g_advancedPermissions || !g_messageHandler) return S_OK;
                                        LPWSTR source = nullptr;
                                        if (FAILED(args->get_Source(&source)) || !localOrigin(source)) {
                                            CoTaskMemFree(source);
                                            return S_OK;
                                        }
                                        CoTaskMemFree(source);
                                        LPWSTR request = nullptr;
                                        if (FAILED(args->TryGetWebMessageAsString(&request)) || !request) return S_OK;
                                        const std::wstring response = g_messageHandler(request);
                                        CoTaskMemFree(request);
                                        return response.empty() ? S_OK : sender->PostWebMessageAsString(response.c_str());
                                    });
                            g_webview->add_WebMessageReceived(webMessageHandler.Get(), &webMessageToken);
                            ComPtr<ICoreWebView2_4> webview4;
                            if (SUCCEEDED(g_webview->QueryInterface(IID_ICoreWebView2_4,
                                    reinterpret_cast<void**>(webview4.GetAddressOf())))) {
                                EventRegistrationToken downloadToken{};
                                auto downloadHandler = makeHandler<ICoreWebView2DownloadStartingEventHandler,
                                    ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs*>(
                                        IID_ICoreWebView2DownloadStartingEventHandler,
                                        [](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                                            return args->put_Cancel(g_advancedPermissions ? FALSE : TRUE);
                                        });
                                webview4->add_DownloadStarting(downloadHandler.Get(), &downloadToken);
                            }
                            hr = g_webview->Navigate(htmlUrl(g_htmlPath).c_str());
                            if (FAILED(hr)) showHtmlFailure(L"Navigating to the local HTML file", hr);
                            return hr;
                        });
                return g_environment->CreateCoreWebView2Controller(g_pane, controllerHandler.Get());
            });
    const HRESULT hr = createEnvironment(nullptr, dataPath.c_str(), nullptr, environmentHandler.Get());
    if (FAILED(hr)) showHtmlFailure(L"Starting WebView2 environment creation", hr);
}

LRESULT CALLBACK paneProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_SIZE) { setBounds(); return 0; }
    if (message == WM_NOTIFY && lParam) {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->hwndFrom == g_navToolbar && header->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMTBCUSTOMDRAW*>(lParam);
            if (draw->nmcd.dwDrawStage == CDDS_PREERASE) {
                FillRect(draw->nmcd.hdc, &draw->nmcd.rc, GetSysColorBrush(COLOR_BTNFACE));
                return CDRF_SKIPDEFAULT;
            }
        }
    }
    if (message == WM_COMMAND && g_webview) {
        switch (LOWORD(wParam)) {
        case HTML_NAV_HOME_BUTTON_ID:
            if (HIWORD(wParam) == 0 && !g_homeUrl.empty()) {
                g_webview->Navigate(g_homeUrl.c_str());
                return 0;
            }
            break;
        case HTML_NAV_BACK_BUTTON_ID:
            if (HIWORD(wParam) == BN_CLICKED) {
                BOOL canGoBack = FALSE;
                if (SUCCEEDED(g_webview->get_CanGoBack(&canGoBack)) && canGoBack)
                    g_webview->GoBack();
                return 0;
            }
            break;
        case HTML_NAV_RELOAD_BUTTON_ID:
            if (HIWORD(wParam) == BN_CLICKED) {
                g_webview->Reload();
                return 0;
            }
            break;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void releaseWebView()
{
    if (g_controller) g_controller->Close();
    g_webview.Reset();
    g_controller.Reset();
    g_environment.Reset();
    g_bridgeInjected = false;
    g_showNavToolbar = false;
}
}

bool openHtmlPane(HWND nppWindow, HINSTANCE module, const std::wstring& htmlPath,
                  const std::wstring& paneTitle, bool advancedPermissions,
                  HtmlWebMessageHandler messageHandler)
{
    const std::wstring previousFolder = g_htmlFolder;
    const bool previousAdvancedPermissions = g_advancedPermissions;
    g_npp = nppWindow;
    g_module = module;
    g_paneTitle = paneTitle.empty() ? L"HTML" : paneTitle;
    g_advancedPermissions = advancedPermissions;
    g_messageHandler = std::move(messageHandler);
    g_htmlPath = canonicalPath(htmlPath);
    if (g_htmlPath.empty()) return false;

    const DWORD fileAttributes = GetFileAttributesW(g_htmlPath.c_str());
    if (fileAttributes == INVALID_FILE_ATTRIBUTES || (fileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxW(g_npp, L"The selected HTML file does not exist.",
                    L"NppMenuScripts HTML", MB_OK | MB_ICONWARNING);
        return false;
    }

    const size_t slash = g_htmlPath.find_last_of(L"\\/");
    g_htmlFolder = canonicalPath(slash == std::wstring::npos ? L"." : g_htmlPath.substr(0, slash));
    const DWORD folderAttributes = g_htmlFolder.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(g_htmlFolder.c_str());
    if (folderAttributes == INVALID_FILE_ATTRIBUTES || !(folderAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxW(g_npp, L"The folder containing the selected HTML file does not exist.",
                    L"NppMenuScripts HTML", MB_OK | MB_ICONWARNING);
        return false;
    }
    g_homeUrl = htmlUrl(g_htmlFolder + L"\\index.html");
    updateNavigationToolbar(false);
    if (g_pane) {
        const bool recreateWebView = g_webview &&
            (previousFolder != g_htmlFolder || previousAdvancedPermissions != g_advancedPermissions);
        g_dock.pszName = g_paneTitle.c_str();
        SetWindowTextW(g_pane, g_paneTitle.c_str());
        SendMessageW(g_npp, NPPM_DMMUPDATEDISPINFO, 0, reinterpret_cast<LPARAM>(g_pane));
        if (recreateWebView) {
            releaseWebView();
            initializeWebView();
        } else if (g_webview) {
            ComPtr<ICoreWebView2Settings> settings;
            if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings)
                settings->put_IsWebMessageEnabled(g_advancedPermissions ? TRUE : FALSE);
            ComPtr<ICoreWebView2_3> webview3;
            if (SUCCEEDED(g_webview->QueryInterface(IID_ICoreWebView2_3,
                    reinterpret_cast<void**>(webview3.GetAddressOf()))) && webview3)
                webview3->SetVirtualHostNameToFolderMapping(HTML_HOST, g_htmlFolder.c_str(),
                    g_advancedPermissions ? COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                                          : COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
            g_webview->Navigate(htmlUrl(g_htmlPath).c_str());
        }
        SendMessageW(g_npp, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(g_pane));
        return true;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = paneProc; wc.hInstance = module;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = HTML_CLASS;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    g_pane = CreateWindowExW(0, HTML_CLASS, g_paneTitle.c_str(), WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                             0, 0, 0, 0, nppWindow, nullptr, module, nullptr);
    if (!g_pane) return false;
    createNavigationToolbar();
    g_dock = {};
    g_dock.hClient = g_pane; g_dock.pszName = g_paneTitle.c_str(); g_dock.dlgID = HTML_DLG_ID;
    g_dock.uMask = DWS_DF_CONT_RIGHT; g_dock.pszModuleName = L"NppMenuScripts";
    if (!SendMessageW(nppWindow, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&g_dock))) {
        DestroyWindow(g_pane); g_pane = nullptr; return false;
    }
    SendMessageW(nppWindow, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(g_pane));
    // CreateWindowEx sends WM_CREATE before it returns, so initialize only
    // after g_pane has received the new HWND.
    initializeWebView();
    return true;
}

void closeHtmlPane()
{
    releaseWebView();
    if (g_navToolbar) DestroyWindow(g_navToolbar);
    g_navToolbar = nullptr;
    if (g_navHomeIcon) DestroyIcon(g_navHomeIcon);
    g_navHomeIcon = nullptr;
    if (g_navBackIcon) DestroyIcon(g_navBackIcon);
    g_navBackIcon = nullptr;
    if (g_navReloadIcon) DestroyIcon(g_navReloadIcon);
    g_navReloadIcon = nullptr;
    if (g_navImageList) ImageList_Destroy(g_navImageList);
    g_navImageList = nullptr;
    g_homeUrl.clear();
    if (g_pane) DestroyWindow(g_pane);
    g_pane = nullptr;
    if (g_loader) { FreeLibrary(g_loader); g_loader = nullptr; }
    if (g_comInitialized) { CoUninitialize(); g_comInitialized = false; }
}

bool isHtmlPaneWindow(HWND hwnd) { return g_pane && hwnd == g_pane; }
bool isHtmlPaneCloseNotification(HWND hwndFrom, UINT_PTR idFrom)
{
    return g_pane && (hwndFrom == g_pane || idFrom == HTML_DLG_ID);
}
