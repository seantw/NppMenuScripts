# NppMenuScripts

[English](README.md) | **中文**

NppMenuScripts 是 Notepad++ 的 x64 外掛程式。它可讓您自訂「選單」或「工具列」，並在這些配置的項目中執行自己撰寫的「腳本語言」或「命令」。


## 功能概述

- 可自行配置「選單」或「工具列」，並指定其「執行動作」。
- 選單支援多層巢狀子選單。最多 16 層子選單 (不含根層)。
- 工具列按鈕可自定圖示。最多 32 個。
- 「執行動作」分三類，包括：
  - **腳本語言**：目前支援三種腳本語言。並提供 JavaScript、Python 及 Perl 的 Helper API 模組，可與編輯中的文件互動。即使未使用 Helper API，一般腳本的 `stdout` 和 `stderr` 也會直接顯示在「輸出面板」。支援基本 ANSI 色彩控制碼。
  - **外掛內建動作**：目前支援兩種動作。「執行目前編輯器中的腳本」與「停止執行中的腳本」。
  - **外部程式 (命令列)**：
    - 可選擇命令列執行方式：輸出面板、Windows 終端機 (Console) 或 Windows 應用程式。
    - 可設定執行前是否自動儲存編輯中的文件。
    - 可設定「輸出面板」的文字編碼。
    - 可透過命令列參數傳入路徑及檔名相關之變數。
    - 可設定執行前的環境變數。除了可帶入路徑及檔名相關變數外，還可帶入目前文件部分內容變數。
- 腳本以獨立程序執行；執行結束後，程序隨即關閉並釋放其資源。沒有執行腳本時，不會有常駐的腳本執行環境。關閉 Notepad++ 時也會清理正在執行的腳本程序。
- 支援多國語言介面。

## 系統需求

- Windows x64
- Notepad++ x64
- 若使用 JavaScript 需安裝 [Node.js® for Windows](https://nodejs.org/en/download/)
- 若使用 Python 需安裝 [Python for Windows](https://www.python.org/downloads/windows/)
- 若使用 Perl 需安裝 [Strawberry Perl for Windows](https://strawberryperl.com/)

## 安裝

1. 編譯或取得 `NppMenuScripts.dll`。
2. 通常在 `C:\Program Files\Notepad++\plugins` 底下建立 `NppMenuScripts` 資料夾。
3. 將 `NppMenuScripts.dll`、`i18n.json`、`menu-helper.js`、`menu_helper.py`、`MenuHelper.pm`、`package.json` 和 `workspace-template` 資料夾放在外掛資料夾。
4. 重啟 Notepad++ 後，首次啟動 NppMenuScripts 會自動建立「工作區資料夾」並放入範例檔案。通常位於 `%APPDATA%\Notepad++\plugins\Config\NppMenuScripts\workspace`。  
如果使用者「工作區資料夾」已存在(且非空)，則不會更動「選單設定」及「使用者腳本」檔案。

典型的資料夾結構如下：

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


## 設定「選單」與「工具列」

透過 [`menu.json`](./workspace-template/menu.json) 來設定「標題文字」並指定其「執行動作」。它長得像這樣：

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

- `menu.json` 必須位於「工作區資料夾」內。檔案為 UTF-8 編碼格式。可從「外掛選單」`開啟 menu.json` 進行編輯、或改變「工作區資料夾」位置。你可自由命名該資料夾名稱，不一定要叫 `workspace`。
- `script_folder`：腳本資料夾。可使用絕對路徑，或以 `menu.json` 所在資料夾為基準的相對路徑，例如 `./script`、`../scripts`。  
若更動了 `script_folder`，請將 `menu-helper.js` 和 `package.json` 拷到該資料夾的上一層，讓 Node.js 能找到 Package Import Alias。
- `icon_folder`：工具列圖示資料夾；相對路徑以 `menu.json` 所在資料夾為基準。
- `menu`：選單設定。可設多層巢狀子選單，最多 16 層子選單 (不含根層)。
- `toolbar`：工具列設定。最多 32 個工具按鈕。
- 選單分隔線使用空物件 `{}`。
- 每個「選單」與「工具」項目必需指定 `title` 及「執行動作」：
  - `title`：(必要)「選單標題」或「工具列按鈕」的提示 (Tooltip)。若要設定快捷鍵，可加上 **Tab 字元** `\t` 和鍵盤快捷鍵名稱。
  - 執行動作 (必要) 有 `script`、`action` 或 `execute` 三種，只能指定其中一項：
    - `script`：值為字串，相對於 `script_folder` 的腳本檔案路徑，或絕對路徑。副檔名支援 `.js`、`.mjs`、`.py`、`.pl`。
    - `action` 值為字串，可設定外掛內建動作或 Notepad++ 選單指令。目前有三種：
      - `"RUN_CURRENT_FILE"` 代表執行目前編輯中的腳本。
      - `"STOP_SCRIPT"` 代表停止執行中的腳本。
      - `"MENU:..."` 執行 Notepad++ 選單。可使用 [Item ID](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/master/PowerEditor/installer/nativeLang/taiwaneseMandarin.xml) 或目前語系「選單路徑字串」。例如 `"MENU: 41001"` 或 `"MENU: 檔案/新增"`。
    - `execute` 物件。屬性如下：
      - `command`：字串(必要)，命令列指令。
      - `run_in`：字串(必要)，執行方式。有下列三種：
        - `output_pane`：直接將文字顯示在「輸出面板」。
        - `console`：另開 Windows 終端機 (Console) 視窗獨立執行。
        - `windows`：一般 Windows 應用程式。
      - `save_before_run`：true/false。執行前自動儲存目前文件。
      - `code_page`：字串或數字。用於 Windows 終端機 (Console) 的字碼頁，或解析「輸出面板」的文字編碼。若未指定，預設為 `ANSI`。常見的有 `ANSI`、`UTF-8`、`UTF-16LE`，或直接指定[代碼頁編號](https://learn.microsoft.com/zh-tw/windows/win32/intl/code-page-identifiers)數字。
      - `env`：執行時附加的環境變數。
      - 上述中的 `command` 及 `env` 可帶入目前文件相關變數：
        - `{FILE_PATH}`：完整路徑檔名
        - `{FILE_NAME}`：檔名 (含副檔名)
        - `{FILE_DIR}`：文件所在路徑
        - `{FILE_EXT}`：副檔名
        - `{LINE_TEXT}`：游標所在整列文字 (僅`env`可使用)
        - `{SELECT_TEXT}`：選取文字 (僅`env`可使用)
  - `icon`：工具列按鈕圖示檔 .ico。尺寸為 32×32 像素、具備 32 位元色彩深度 (支援全彩與透明度)。僅在 `toolbar` 內有作用。
  - `sub`：子選單項目陣列，可包含子選單或腳本項目。僅在 `menu` 內有作用。


### 小訣竅
- 修改並儲存 menu.json 後可選擇「外掛選單」重建選單與工具列，不需重啟 Notepad++ 即可審視設定結果。
- 按住 Ctrl 並點擊自訂的「選單項目」或「工具列按鈕」，可直接開啟對應腳本進行編輯；檔案不存在時可選擇建立範本。
- 可從「外掛選單」選擇「停止腳本」，強制終止正在執行的腳本及由它啟動、受管理的子程序。


### 鍵盤快捷鍵

每個「選單項目名稱」後面，可以使用 **Tab 字元 `\t`** 加上快捷按鍵：

```json
{ "title": "My Script\tCtrl+Shift+R", "script": "my-script.js" }
```

支援的修飾鍵：

| 修飾鍵 | 寫法 |
|---|---|
| Ctrl | `Ctrl` 或 `Control` |
| Shift | `Shift` |
| Alt | `Alt` |

支援的按鍵：

| 按鍵 | 寫法 |
|---|---|
| 英文字母／數字 | `A`–`Z`、`0`–`9` |
| 功能鍵 | `F1`–`F24` |
| Tab | `Tab` |

快捷鍵必須至少包含一個修飾鍵，例如 `Ctrl+1`、`Alt+F8` 或 `Shift+Tab`；單獨的 `Tab` 不會註冊。快捷鍵透過 Windows `RegisterHotKey` 註冊，如果組合已被 Windows 或其他程式占用，就可能無法使用。

**Access Keys**：按下 Alt 鍵，搭配根層選單名稱 & 符號後的英文字母 (通常會出現底線)，即可打開該選單。


## 腳本語言

您的腳本檔案請以 **UTF-8** 編碼儲存，尤其是含有中文等非 ASCII 字元時。

初次安裝此外掛後，可從「外掛選單」選擇 `開啟腳本資料夾` 查看內附的腳本範例；也可按住 Ctrl 並點擊「腳本選單項目」，直接開啟範例原始碼。

### JavaScript Helper API



#### CommonJS 與 ES Module

```js
/* CommonJS (.js) */
const npp = require('#menu-helper');
```
或：
```js
/* ES Module (.mjs) */
import npp from '#menu-helper';
```

### Promise 非同步 API
非同步 (Async) 工作，請使用 Promise API。各 Method 回傳值會以 Promise 包裝。範例請參考 `workspace-template/script/test-async-alert.js`。

```js
/* CommonJS (.js) */
const npp = require('#menu-helper').promises;
```
或：
```js
/* ES Module (.mjs) */
import nppModule from '#menu-helper';
const npp = nppModule.promises;
```


**注意事項：**
- 在同一支 JavaScript 中不可同時混用「同步」與「非同步」。
- 若您曾複製或移動 Helper API 模組，當模組與新版 `NppMenuScripts.dll` 不相容時，首次呼叫 Helper API 會提示您更新該模組。


### Python Helper API

Python 腳本匯入 Helper API，並使用 `npp` 作為別名：

```python
import menu_helper as npp

npp.alert("Hello from Python")
```

NppMenuScripts 會將外掛資料夾加入 Python 模組搜尋路徑，讓 Python 可以載入 `menu_helper.py`。

### Perl Helper API

Perl 腳本載入 `MenuHelper.pm`，建立物件後使用 Helper API：

```perl
use MenuHelper ();
my $npp = MenuHelper->new();

$npp->alert('Hello from Perl');
```

NppMenuScripts 會將外掛資料夾加入 Perl 模組搜尋路徑，讓 Perl 可以載入 `MenuHelper.pm`。

### API 參考

#### 對話框
| API | 回傳值 | 說明 |
|---|---|---|
| 💬`alert(message, [title], [icon])` | `void` | 訊息對話框 |
| 💬`prompt(message, [defaultValue], [title], [icon])` | `string / null` | 提示使用者輸入文字；按取消時回傳 `null` |
| 💬`confirm(message, [title], [icon])` | `true / false` | 確認對話框 |

`icon` 可使用系統圖示名稱 `error`、`warning`、`info`、`question`，或簡寫 `x`、`!`、`i`、`?`；也可指定 `.ico` 檔名，從 `icon_folder` 設定的資料夾載入。

若要指定 `icon` 但省略前面的選用參數，JavaScript 可傳入 `undefined`、Python 可使用 `icon=` 關鍵字、Perl 則傳入 `undef`。

#### 編輯中的文字
| API | 回傳值 | 說明 |
|---|---|---|
| 📤`getLine()`<br>📥`setLine(text)` | `string / boolean` | 讀取或置換游標所在**列文字** |
| 📤`getSelection()`<br>📥`setSelection(text)`<br>❓ `hasSelection()` | `string / boolean / boolean` | 讀取或置換目前**選取文字**。若沒有選取，則在游標處插入文字。 |
| 📤`getText()`<br>📥`setText(text)` | `string / boolean` | 讀取或置換當前文件**全部文字** |
| 📤`getClipBoard([format])`<br>📥`setClipBoard(text)` | `string / boolean` | 讀取或置換**剪貼簿**文字。<br>`format` 參數可為 `"HTML"`(非必要)，若剪貼簿非 HTML 格式則回傳空字串。 |



#### 游標及捲軸
| API | 回傳值 | 說明 |
|---|---|---|
| 📤`getCursor()`<br>📥`setCursor(line, column)` | `{line: number, column: number} / boolean` | 取得或設定目前游標位置 (0 起算) |
| 📤`getScrollLine()`<br>📥`setScrollLine(line)` | `number / boolean` | 取得或設定編輯區捲動行號 (0 起算)；若在摺疊中會自動展開，若該行隱藏則回傳 `false` |

#### 檔案
| API | 回傳值 | 說明 |
|---|---|---|
| 📤`getFileName()` | `string` | 取得目前文件完整路徑 |
| 📤`getOpenFiles()` | `string[]` | 回傳所有已開啟且有檔名的文件完整路徑 |
| ⚡ `openFile(fullPathFileName)` | `boolean` | 開啟檔案並切換至該文件 |
| ⚡ `openFileInBackground(fullPathFileName)` | `boolean` | 開啟檔案並保留目前作用中的文件 |
| ⚡ `saveFile()` | `boolean` | 儲存目前文件；成功回傳 `true`，若文件未命名或儲存失敗則回傳 `false` |
| ⚡ `closeFile(fullPathFileName)` | `boolean` | 關閉指定檔案；若使用者取消未儲存提示則回傳 `false` |

#### 其他
| API | 回傳值 | 說明 |
|---|---|---|
| 📥`setStatusBarText(text)` | `boolean` | 設定 Notepad++ 狀態列文字 |
| 📥`setOutputPaneTitle(text)` | `boolean` | 設定「輸出面板」標題 |
| ⚡ `runMenu(command)` | `boolean` | 執行 Notepad++ 選單。`command`參數可使用 [Item ID](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/master/PowerEditor/installer/nativeLang/taiwaneseMandarin.xml) 或目前語系「選單路徑字串」。例如：`41001` 或 `"檔案/新增"` |


**注意事項：**
- 布林值在 Python 回傳 True/False；在 Perl 回傳 1/0。
- 各腳本語言的 Helper API 為同步呼叫 (Synchronous)。僅 JavaScript 另外提供非同步呼叫 (Asynchronous)。
- 參數中的檔案路徑分隔字元 `\` 及 `/` 皆通用。在字串中若要使用 `\` 記得跳脫，像這樣 `\\`。


## 多國語言

多國語言檔位於 `NppMenuScripts.dll` 相同目錄下的 `i18n.json` 檔。Notepad++ 啟動時載入。依 Notepad++ 目前語系顯示。您可自行在此添加您的語言。原始檔位於 [`support/i18n.json`](support/i18n.json)。

語言代碼請參考 [Notepad++ User Manual](https://npp-user-manual.org/docs/command-prompt/#langCodeList)，點擊 "show Language Codes" 展開。


## 編譯 NppMenuScripts.dll

`build-x64.bat` 使用 MinGW / Strawberry GCC。

1. 執行 `get-cjson.bat` 下載所需的 cJSON 原始碼。
2. 確認 `g++` 已在環境變數 `PATH` 中可執行。
3. 執行 `build-x64.bat`。
4. 產出 DLL 於 `build/NppMenuScripts.dll`。

`test-x64.bat` 可執行不需啟動 Notepad++ 的單元測試。

## License

NppMenuScripts 採用 **MIT License** 授權。詳細內容請參閱 [LICENSE](LICENSE)。
