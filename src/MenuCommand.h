#pragma once

#include <string_view>
#include <string>

enum class MenuCommandAction {
    RunScript,
    RunCommand,
    RunCurrentFile,
    StopScript,
    RunMenuCommand
};

constexpr std::wstring_view RUN_CURRENT_FILE_ACTION = L"RUN_CURRENT_FILE";
constexpr std::wstring_view STOP_SCRIPT_ACTION = L"STOP_SCRIPT";

constexpr bool hasExactlyOneExecutionTarget(bool hasScript, bool hasAction)
{
    return hasScript != hasAction;
}

constexpr bool hasExactlyOneExecutionTarget(bool hasScript, bool hasAction, bool hasExecute)
{
    return static_cast<unsigned int>(hasScript) + static_cast<unsigned int>(hasAction) +
           static_cast<unsigned int>(hasExecute) == 1;
}

inline bool parseActionToken(std::wstring_view value, MenuCommandAction& action, std::wstring& target)
{
    target.clear();
    if (value == RUN_CURRENT_FILE_ACTION) {
        action = MenuCommandAction::RunCurrentFile;
        return true;
    }
    if (value == STOP_SCRIPT_ACTION) {
        action = MenuCommandAction::StopScript;
        return true;
    }
    constexpr std::wstring_view menuPrefix = L"MENU:";
    if (value.size() >= menuPrefix.size() && value.substr(0, menuPrefix.size()) == menuPrefix) {
        size_t first = menuPrefix.size();
        while (first < value.size() && (value[first] == L' ' || value[first] == L'\t')) ++first;
        size_t last = value.size();
        while (last > first && (value[last - 1] == L' ' || value[last - 1] == L'\t' ||
                                value[last - 1] == L'\r' || value[last - 1] == L'\n')) --last;
        if (last == first) return false;
        target.assign(value.substr(first, last - first));
        action = MenuCommandAction::RunMenuCommand;
        return true;
    }
    return false;
}
