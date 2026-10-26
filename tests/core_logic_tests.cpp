#include "../src/MenuCommand.h"
#include "../src/ScriptExitState.h"

#include <cassert>

int main()
{
    assert(classifyScriptExit(0, false, false) == ScriptExitState::Completed);
    assert(classifyScriptExit(0, false, true) == ScriptExitState::CompletedWithCustomStatus);
    assert(classifyScriptExit(1, false, false) == ScriptExitState::Failed);
    assert(classifyScriptExit(0, true, false) == ScriptExitState::Stopped);
    assert(classifyScriptExit(1, true, false) == ScriptExitState::Stopped);
    assert(classifyScriptExit(0, true, true) == ScriptExitState::Stopped);

    assert(hasExactlyOneExecutionTarget(true, false));
    assert(hasExactlyOneExecutionTarget(false, true));
    assert(!hasExactlyOneExecutionTarget(false, false));
    assert(!hasExactlyOneExecutionTarget(true, true));
    MenuCommandAction action{};
    std::wstring target;
    assert(parseActionToken(L"RUN_CURRENT_FILE", action, target));
    assert(action == MenuCommandAction::RunCurrentFile);
    assert(target.empty());
    assert(parseActionToken(L"STOP_SCRIPT", action, target));
    assert(action == MenuCommandAction::StopScript);
    assert(target.empty());
    assert(parseActionToken(L"MENU: 41001", action, target));
    assert(action == MenuCommandAction::RunMenuCommand);
    assert(target == L"41001");
    assert(parseActionToken(L"MENU:Plugins/NppExec/Execute...", action, target));
    assert(target == L"Plugins/NppExec/Execute...");
    assert(!parseActionToken(L"MENU: ", action, target));
    assert(!parseActionToken(L"{RUN_CURRENT_FILE}", action, target));
    assert(!parseActionToken(L"{CURRENT_FILE}", action, target));
    assert(!parseActionToken(L"{UNKNOWN}", action, target));
}
