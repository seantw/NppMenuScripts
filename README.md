# NppMenuScripts

**English** | [中文](README.zh-TW.md)

NppMenuScripts is an x64 plugin for Notepad++. It lets you configure menus and toolbars to run your own scripts or commands.

## Features

- Configure menus and toolbars, and assign actions to their items.
- Menus support nested submenus up to 16 levels, excluding the root menu.
- Up to 32 toolbar buttons can use custom icons.
- Actions fall into three categories:
  - **Scripts**: JavaScript, Python, and Perl are supported. Helper API modules are available for all three languages to interact with the active document. Even without the Helper API, script `stdout` and `stderr` are displayed in the Output Pane. Basic ANSI color codes are supported.
  - **Built-in plugin actions**: Run the script currently open in the editor, or stop a running script.
  - **External programs (command line)**:
    - Options include running a command in the Output Pane, in a Windows Console window, or as a Windows application.
    - Optionally save the active document before running.
    - Set the text encoding used by the Output Pane.
    - Pass file path and name placeholders as command-line arguments.
    - Set environment variables for the process, including placeholders for parts of the active document.
- Scripts run in separate processes. Each process exits and releases its resources when it finishes. No script runtime stays resident when no script is running. Running script processes are also cleaned up when Notepad++ shuts down.
- The interface supports multiple languages.

## Requirements

- Windows x64
- Notepad++ x64
- For JavaScript, install [Node.js® for Windows](https://nodejs.org/en/download/).
- For Python, install [Python for Windows](https://www.python.org/downloads/windows/).
- For Perl, install [Strawberry Perl for Windows](https://strawberryperl.com/).

## Installation

1. Build or obtain `NppMenuScripts.dll`.
2. Create a `NppMenuScripts` folder under `Notepad++\plugins`, usually at `C:\Program Files\Notepad++\plugins`.
3. Place `NppMenuScripts.dll`, `i18n.json`, `menu-helper.js`, `menu_helper.py`, `MenuHelper.pm`, `package.json`, and the `workspace-template` folder in the plugin folder.
4. Restart Notepad++. On its first launch, NppMenuScripts creates the workspace folder and copies the example files. It is usually located at `%APPDATA%\Notepad++\plugins\Config\NppMenuScripts\workspace`.  
If the workspace folder already exists and is not empty, its menu settings and user scripts are left unchanged.

A typical folder structure is:

```text
Notepad++\
├─ plugins\
│  └─ NppMenuScripts\
│     ├─ NppMenuScripts.dll
│     ├─ i18n.json
│     ├─ menu-helper.js
│     ├─ menu_helper.py
│     ├─ MenuHelper.pm
│     ├─ package.json
│     └─ workspace-template\
│        ├─ menu.json
│        ├─ icon\
│        │  ├─ calc.ico
│        │  ├─ hello.ico
│        │  ├─ run.ico
│        │  └─ stop.ico
│        └─ script\
│           ├─ hello-world.js
│           ├─ hello-world.py
│           ├─ test-execute.vbs
│           └─ test.js
```

## Configure menus and toolbars

Use [`menu.json`](./workspace-template/menu.json) to configure item titles and actions. For example:

```json
{
  "script_folder": "./script",
  "icon_folder": "./icon",
  "menu": {
    "&Hello": [
      { "title": "Hello World\tCtrl+1", "script": "hello-world.js" },
      {
        "title": "Python Tests",
        "sub": [
          { "title": "Hello World", "script": "hello-world.py" }
        ]
      }
    ],
    "&Document": [
      { "title": "Format Document", "script": "format.pl" }
    ]
  },
  "toolbar": [
    { "icon": "hello.ico", "title": "Run Hello World",    "script": "hello-world.js" },
    { "icon": "run.ico",   "title": "Run Current Script", "action": "RUN_CURRENT_FILE" },
    { "icon": "calc.ico",  "title": "Open Calculator",    "execute": { "command": "calc.exe", "run_in": "windows" } }
  ]
}
```

- `menu.json` must be in the workspace folder and use UTF-8 encoding. Choose **Open menu.json** from the plugin menu to edit it or change the workspace folder. You can name the workspace folder anything you like; it does not have to be named `workspace`.
- `script_folder`: The script folder. Use an absolute path or a relative path based on the folder containing `menu.json`, such as `./script` or `../scripts`.  
If you change `script_folder`, copy `menu-helper.js` and `package.json` to its parent folder so Node.js can resolve the package import alias.
- `icon_folder`: The toolbar icon folder. Relative paths are based on the folder containing `menu.json`.
- `menu`: Menu settings. Nested submenus can be up to 16 levels deep, excluding the root menu.
- `toolbar`: Toolbar settings. Up to 32 buttons are supported.
- Use an empty object `{}` for a menu separator.
- Each menu item and toolbar item must have a `title` and one action:
  - `title` (required): The menu item label or toolbar button tooltip. To assign a keyboard shortcut, append a **tab character** `\t` followed by the shortcut name.
  - Choose exactly one of `script`, `action`, or `execute`:
    - `script`: A script file path, relative to `script_folder` or absolute.
    - `action`: A built-in plugin action or Notepad++ menu command. The available actions are:
      - `"RUN_CURRENT_FILE"` runs the script currently open in the editor.
      - `"STOP_SCRIPT"` stops the running script.
      - `"MENU:..."` runs a Notepad++ menu command. Use an [Item ID](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/master/PowerEditor/installer/nativeLang/english.xml) or a menu path in the current UI language. For example, `"MENU: 41001"` or `"MENU: File/New"`.
    - `execute`: An object with the following properties:
      - `command` (required): The command line to run.
      - `run_in` (required): How to run it:
        - `output_pane`: Displays output in the Output Pane.
        - `console`: Opens a separate Windows Console window and runs the command independently.
        - `windows`: Runs a regular Windows application.
      - `save_before_run`: `true` or `false`. Saves the active document before running.
      - `code_page`: A string or number. Sets the code page for the Windows Console or the text encoding used to parse Output Pane output. The default is `ANSI`. Common values include `ANSI`, `UTF-8`, and `UTF-16LE`; you can also specify a [Windows code page identifier](https://learn.microsoft.com/en-us/windows/win32/intl/code-page-identifiers).
      - `env`: Environment variables added when running the command.
      - The `command` and `env` values can use placeholders for the active document:
        - `{FILE_PATH}`: Full file path
        - `{FILE_NAME}`: File name, including extension
        - `{FILE_DIR}`: Folder containing the file
        - `{FILE_EXT}`: File extension
        - `{LINE_TEXT}`: Text on the current line (available in `env` only)
        - `{SELECT_TEXT}`: Selected text (available in `env` only)
  - `icon`: A toolbar button icon file in `.ico` format, 32×32 pixels with 32-bit color depth (full color and transparency). This property only applies to `toolbar` items.
  - `sub`: An array of submenu items, which can contain nested submenus or script items. This property only applies to `menu` items.

### Tips

- After editing and saving `menu.json`, choose **Rebuild Menu & Toolbar** from the plugin menu to apply the changes. Notepad++ does not need to be restarted.
- Hold Ctrl and click a custom menu item or toolbar button to open its script for editing. If the file does not exist, you can choose to create a template.
- Choose **Stop Script** from the plugin menu to forcefully terminate a running script and its managed child processes.

### Keyboard shortcuts

Append a shortcut to a menu item title using a **tab character `\t`**:

```json
{ "title": "My Script\tCtrl+Shift+R", "script": "my-script.js" }
```

Supported modifier keys:

| Modifier | Syntax |
|---|---|
| Ctrl | `Ctrl` or `Control` |
| Shift | `Shift` |
| Alt | `Alt` |

Supported keys:

| Key | Syntax |
|---|---|
| Letters and digits | `A`–`Z`, `0`–`9` |
| Function keys | `F1`–`F24` |
| Tab | `Tab` |

Each shortcut must include at least one modifier key, such as `Ctrl+1`, `Alt+F8`, or `Shift+Tab`. A standalone `Tab` is not registered. Shortcuts use Windows `RegisterHotKey`, so a combination already used by Windows or another application may not be available.

**Access keys**: Press Alt and the letter following `&` in a root menu title (usually shown with an underline) to open that menu.

## Script languages

Save your script files as **UTF-8**, especially when they contain non-ASCII characters such as Chinese.

After installing the plugin, choose **Open Scripts Folder** from the plugin menu to view the included examples. You can also hold Ctrl and click a script menu item to open its source in Notepad++.

### JavaScript Helper API

#### CommonJS and ES Modules

```js
/* CommonJS (.js) */
const npp = require('#menu-helper');
```

Or:

```js
/* ES Module (.mjs) */
import npp from '#menu-helper';
```

### Promise-based asynchronous API

For asynchronous work, use the Promise API. Each method returns a Promise. See `workspace-template/script/test-async-alert.js` for an example.

```js
/* CommonJS (.js) */
const npp = require('#menu-helper').promises;
```

Or:

```js
/* ES Module (.mjs) */
import nppModule from '#menu-helper';
const npp = nppModule.promises;
```

**Notes:**

- Do not mix the synchronous and asynchronous APIs in the same script.
- If you copied or moved a Helper API module, NppMenuScripts will notify you the first time the script calls the Helper API if the module is incompatible with the loaded `NppMenuScripts.dll`.

### Python Helper API

Python scripts can import the Helper API using `npp` as an alias:

```python
import menu_helper as npp

npp.alert("Hello from Python")
```

NppMenuScripts adds the plugin folder to Python's module search path so Python can load `menu_helper.py`.

### Perl Helper API

Perl scripts can load `MenuHelper.pm`, create an object, and use the Helper API:

```perl
use MenuHelper ();
my $npp = MenuHelper->new();

$npp->alert('Hello from Perl');
```

NppMenuScripts adds the plugin folder to Perl's module search path so Perl can load `MenuHelper.pm`.

### API reference

#### Dialogs

| API | Returns | Description |
|---|---|---|
| 💬`alert(message, [title], [icon])` | `void` | Shows a message dialog. |
| 💬`prompt(message, [defaultValue], [title], [icon])` | `string / null` | Prompts the user for text. Returns `null` when cancelled. |
| 💬`confirm(message, [title], [icon])` | `true / false` | Shows a confirmation dialog. |

`icon` accepts the system icon names `error`, `warning`, `info`, and `question`, or the short forms `x`, `!`, `i`, and `?`. It can also be an `.ico` filename loaded from the folder configured by `icon_folder`.

To provide `icon` while omitting earlier optional parameters, pass `undefined` in JavaScript, use the `icon=` keyword in Python, or pass `undef` in Perl.

#### Current document text

| API | Returns | Description |
|---|---|---|
| 📤`getLine()`<br>📥`setLine(text)` | `string / boolean` | Gets or replaces the text on the current **line**. |
| 📤`getSelection()`<br>📥`setSelection(text)`<br>❓ `hasSelection()` | `string / boolean / boolean` | Gets or replaces the **selected text**. If there is no selection, `setSelection()` inserts text at the cursor. |
| 📤`getText()`<br>📥`setText(text)` | `string / boolean` | Gets or replaces **all text** in the current document. |
| 📤`getClipBoard([format])`<br>📥`setClipBoard(text)` | `string / boolean` | Gets or replaces **clipboard text**.<br>The optional `format` parameter can be `"HTML"`. If the clipboard is not in HTML format, an empty string is returned. |

#### Cursor and scrolling

| API | Returns | Description |
|---|---|---|
| 📤`getCursor()`<br>📥`setCursor(line, column)` | `{line: number, column: number} / boolean` | Gets or sets the cursor position (0-based). |
| 📤`getScrollLine()`<br>📥`setScrollLine(line)` | `number / boolean` | Gets or sets the editor scroll line (0-based). A folded section is expanded automatically; returns `false` if the line is hidden. |

#### Files

| API | Returns | Description |
|---|---|---|
| 📤`getFileName()` | `string` | Returns the full path of the current document. |
| 📤`getOpenFiles()` | `string[]` | Returns the full paths of all open, named documents. |
| ⚡ `openFile(fullPathFileName)` | `boolean` | Opens a file and switches to it. |
| ⚡ `openFileInBackground(fullPathFileName)` | `boolean` | Opens a file and keeps the current document active. |
| ⚡ `saveFile()` | `boolean` | Saves the current document. Returns `true` on success and `false` if the document is unnamed or saving fails. |
| ⚡ `closeFile(fullPathFileName)` | `boolean` | Closes the specified file. Returns `false` if the user cancels the unsaved-changes prompt. |

#### Other

| API | Returns | Description |
|---|---|---|
| 📥`setStatusBarText(text)` | `boolean` | Sets the Notepad++ status bar text. |
| 📥`setOutputPaneTitle(text)` | `boolean` | Sets the Output Pane title. |
| ⚡ `runMenu(command)` | `boolean` | Runs a Notepad++ menu command. The `command` parameter accepts an [Item ID](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/master/PowerEditor/installer/nativeLang/english.xml) or a menu path string in the current UI language. For example: `41001` or `"File/New"`. |

**Notes:**

- Boolean values are returned as `True`/`False` in Python and `1`/`0` in Perl.
- The Helper API is synchronous in all supported languages. JavaScript also provides asynchronous calls.
- File paths accept either `\` or `/` as separators. Escape backslashes as `\\` when writing them in strings.

## Multilingual support

The language file is `i18n.json`, located in the same folder as `NppMenuScripts.dll`. It is loaded when Notepad++ starts and the interface follows the current Notepad++ language. You can add your own languages to this file. The source file is in [`support/i18n.json`](support/i18n.json).

For language codes, see the [Notepad++ User Manual](https://npp-user-manual.org/docs/command-prompt/#langCodeList) and expand **show Language Codes**.

## Build NppMenuScripts.dll

`build-x64.bat` uses MinGW / Strawberry GCC.

1. Run `get-cjson.bat` to download the required cJSON source files.
2. Make sure `g++` is available on your `PATH`.
3. Run `build-x64.bat`.
4. The DLL is created at `build/NppMenuScripts.dll`.

Run `test-x64.bat` to execute unit tests that do not require starting Notepad++.

## License

NppMenuScripts is released under the **MIT License**. See [LICENSE](LICENSE).
