#pragma once

#include <windows.h>
#include <functional>
#include <string>

using HtmlWebMessageHandler = std::function<std::wstring(const std::wstring&)>;
bool openHtmlPane(HWND nppWindow, HINSTANCE module, const std::wstring& htmlPath,
                  const std::wstring& paneTitle, bool advancedPermissions,
                  HtmlWebMessageHandler messageHandler);
void closeHtmlPane();
bool isHtmlPaneWindow(HWND hwnd);
bool isHtmlPaneCloseNotification(HWND hwndFrom, UINT_PTR idFrom);
