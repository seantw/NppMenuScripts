#pragma once

#include <windows.h>
#include <functional>
#include <string>

struct cJSON;

enum class MenuConfigEditorResult { Closed, WorkspaceChanged };

struct MenuConfigEditorContext {
    HWND owner = nullptr;
    HINSTANCE module = nullptr;
    std::wstring menuPath;
    std::wstring workspaceFolder;
    std::wstring scriptFolder;
    std::wstring iconFolder;
    std::wstring placeholdersUrl;
    std::wstring scriptRequirementsUrl;
    std::wstring scriptTemplateUrl;
    std::wstring scriptApiUrl;
    std::wstring initialStatus;
    std::function<std::wstring(const char*)> translate;
    std::function<bool(cJSON*, std::wstring&)> validate;
    std::function<bool(std::wstring&, std::wstring&)> saveAndRebuild;
    std::function<bool(HWND, std::wstring&)> chooseWorkspaceFolder;
    std::function<bool(HWND, const std::wstring&)> openOrCreateScript;
};

MenuConfigEditorResult showMenuConfigEditor(const MenuConfigEditorContext& context,
                                            std::wstring* statusAfterWorkspaceChange = nullptr);
