'use strict';

const fs = require('node:fs');

// Keep this synchronized with HELPER_API_ID in src/main.cpp.
const HELPER_API_ID = '12';
const HELPER_API_CHECK_PREFIX = '\x1eNPPMENUSCRIPTS_HELPER_API:';
const ALERT_PREFIX = '\x1eNPPMENUSCRIPTS_ALERT:';
const PROMPT_PREFIX = '\x1eNPPMENUSCRIPTS_PROMPT:';
const CONFIRM_PREFIX = '\x1eNPPMENUSCRIPTS_CONFIRM:';
const GETFILE_PREFIX = '\x1eNPPMENUSCRIPTS_GETFILE:';
const SAVEFILE_PREFIX = '\x1eNPPMENUSCRIPTS_SAVEFILE:';
const GETOPENFILES_PREFIX = '\x1eNPPMENUSCRIPTS_GETOPENFILES:';
const CLOSEFILE_PREFIX = '\x1eNPPMENUSCRIPTS_CLOSEFILE:';
const OPENFILE_PREFIX = '\x1eNPPMENUSCRIPTS_OPENFILE:';
const OPENFILEINBACKGROUND_PREFIX = '\x1eNPPMENUSCRIPTS_OPENFILEINBACKGROUND:';
const GETCURSOR_PREFIX = '\x1eNPPMENUSCRIPTS_GETCURSOR:';
const SETCURSOR_PREFIX = '\x1eNPPMENUSCRIPTS_SETCURSOR:';
const GETSCROLLLINE_PREFIX = '\x1eNPPMENUSCRIPTS_GETSCROLLLINE:';
const SETSCROLLLINE_PREFIX = '\x1eNPPMENUSCRIPTS_SETSCROLLLINE:';
const GETLINE_PREFIX = '\x1eNPPMENUSCRIPTS_GETLINE:';
const SETLINE_PREFIX = '\x1eNPPMENUSCRIPTS_SETLINE:';
const GETSELECTION_PREFIX = '\x1eNPPMENUSCRIPTS_GETSELECTION:';
const SETSELECTION_PREFIX = '\x1eNPPMENUSCRIPTS_SETSELECTION:';
const HASSELECTION_PREFIX = '\x1eNPPMENUSCRIPTS_HASSELECTION:';
const GETTEXT_PREFIX = '\x1eNPPMENUSCRIPTS_GETTEXT:';
const SETTEXT_PREFIX = '\x1eNPPMENUSCRIPTS_SETTEXT:';
const GETCLIPBOARD_PREFIX = '\x1eNPPMENUSCRIPTS_GETCLIPBOARD:';
const SETCLIPBOARD_PREFIX = '\x1eNPPMENUSCRIPTS_SETCLIPBOARD:';
const RUNMENUCOMMAND_PREFIX = '\x1eNPPMENUSCRIPTS_RUNMENUCOMMAND:';
const SETSTATUSBARTEXT_PREFIX = '\x1eNPPMENUSCRIPTS_SETSTATUSBARTEXT:';
const SETOUTPUTPANETITLE_PREFIX = '\x1eNPPMENUSCRIPTS_SETOUTPUTPANETITLE:';

let stdinBuffer = '';
let stdinReady = false;
let stdinWaiter = null;
let syncStdinBuffer = '';
let protocolMode = null;
let helperApiChecked = false;

function ensureHelperApiCompatibility() {
  if (helperApiChecked) return;

  const idArgIndex = process.argv.indexOf('--nppmenuscripts-helper-api-id');
  const pluginApiId = idArgIndex >= 0 ? process.argv[idArgIndex + 1] : undefined;
  if (pluginApiId !== HELPER_API_ID) {
    throw new Error(
      `NppMenuScripts menu-helper.js (API ID ${HELPER_API_ID}) is incompatible with ` +
      `the loaded NppMenuScripts.dll (API ID ${pluginApiId || 'missing'}). ` +
      'Copy the matching menu-helper.js from Notepad++\\plugins\\NppMenuScripts ' +
      'to the directory above script_folder.'
    );
  }

  fs.writeSync(1, `${HELPER_API_CHECK_PREFIX}${HELPER_API_ID}\n`, null, 'utf8');
  helperApiChecked = true;
}

function selectProtocolMode(mode) {
  if (protocolMode && protocolMode !== mode) {
    throw new Error('Do not mix the synchronous and promises menu-helper APIs in one script');
  }
  protocolMode = mode;
}

function getDefaultAlertTitle() {
  const args = process.argv;
  const index = args.indexOf('--nppmenuscripts-menu-title-b64');
  if (index >= 0 && index + 1 < args.length) {
    try {
      return Buffer.from(args[index + 1], 'base64').toString('utf8');
    } catch (_) {
      // Fall through to the environment variable.
    }
  }
  return String(process.env.NPPMENUSCRIPTS_MENU_TITLE || 'NppMenuScripts');
}

function finishStdinWaiter() {
  if (!stdinWaiter) return false;
  const newline = stdinBuffer.indexOf('\n');
  if (newline < 0) return false;

  let line = stdinBuffer.slice(0, newline);
  stdinBuffer = stdinBuffer.slice(newline + 1);
  if (line.endsWith('\r')) line = line.slice(0, -1);

  const waiter = stdinWaiter;
  stdinWaiter = null;
  process.stdin.pause();
  if (typeof process.stdin.unref === 'function') process.stdin.unref();
  waiter.resolve(line);
  return true;
}

function installStdinReader() {
  if (stdinReady) return;
  stdinReady = true;
  process.stdin.setEncoding('utf8');
  process.stdin.on('data', (chunk) => {
    stdinBuffer += String(chunk);
    finishStdinWaiter();
  });
  process.stdin.on('error', (error) => {
    if (stdinWaiter) {
      const waiter = stdinWaiter;
      stdinWaiter = null;
      waiter.reject(error);
    }
  });
}

function readResponseLine() {
  installStdinReader();

  if (finishStdinWaiter()) {
    // The function above resolves an existing waiter, so this branch is only
    // relevant when a future refactor leaves one behind.
  }

  const newline = stdinBuffer.indexOf('\n');
  if (newline >= 0) {
    let line = stdinBuffer.slice(0, newline);
    stdinBuffer = stdinBuffer.slice(newline + 1);
    if (line.endsWith('\r')) line = line.slice(0, -1);
    return Promise.resolve(line);
  }

  return new Promise((resolve, reject) => {
    stdinWaiter = { resolve, reject };
    if (typeof process.stdin.ref === 'function') process.stdin.ref();
    process.stdin.resume();
  });
}

function requestResponse(payload) {
  try {
    selectProtocolMode('promises');
    ensureHelperApiCompatibility();
  } catch (error) {
    return Promise.reject(error);
  }
  installStdinReader();
  if (stdinWaiter) {
    return Promise.reject(new Error('NppMenuScripts protocol request is already pending'));
  }

  if (typeof process.stdin.ref === 'function') process.stdin.ref();
  process.stdin.resume();

  return new Promise((resolve, reject) => {
    stdinWaiter = { resolve, reject };
    process.stdout.write(payload, (error) => {
      if (error) {
        const waiter = stdinWaiter;
        stdinWaiter = null;
        process.stdin.pause();
        if (typeof process.stdin.unref === 'function') process.stdin.unref();
        if (waiter) waiter.reject(error);
      }
    });
  });
}

function requestResponseSync(payload) {
  selectProtocolMode('sync');
  ensureHelperApiCompatibility();
  if (stdinWaiter) throw new Error('NppMenuScripts protocol request is already pending');
  fs.writeSync(1, payload, null, 'utf8');

  while (true) {
    const newline = syncStdinBuffer.indexOf('\n');
    if (newline >= 0) {
      let line = syncStdinBuffer.slice(0, newline);
      syncStdinBuffer = syncStdinBuffer.slice(newline + 1);
      if (line.endsWith('\r')) line = line.slice(0, -1);
      return line;
    }

    const chunk = Buffer.allocUnsafe(4096);
    const bytesRead = fs.readSync(0, chunk, 0, chunk.length, null);
    if (bytesRead === 0) throw new Error('NppMenuScripts closed the protocol input');
    syncStdinBuffer += chunk.toString('utf8', 0, bytesRead);
  }
}

function writeAlert(message, title, icon) {
  const text = String(message ?? '');
  const alertTitle = title == null ? getDefaultAlertTitle() : String(title);
  const iconName = arguments.length >= 3 ? String(icon ?? '') : '';
  const encodedMessage = Buffer.from(text, 'utf8').toString('base64');
  const encodedTitle = Buffer.from(alertTitle, 'utf8').toString('base64');
  const encodedIcon = Buffer.from(iconName, 'utf8').toString('base64');
  return requestResponse(`${ALERT_PREFIX}${encodedMessage}|${encodedTitle}|${encodedIcon}\n`).then(() => undefined);
}

function prompt(message, defaultValue, title, icon) {
  const text = String(message ?? '');
  const defaultText = arguments.length >= 2 ? String(defaultValue ?? '') : '';
  const promptTitle = title == null ? getDefaultAlertTitle() : String(title);
  const iconName = arguments.length >= 4 ? String(icon ?? '') : '';
  const encodedMessage = Buffer.from(text, 'utf8').toString('base64');
  const encodedDefault = Buffer.from(defaultText, 'utf8').toString('base64');
  const encodedTitle = Buffer.from(promptTitle, 'utf8').toString('base64');
  const encodedIcon = Buffer.from(iconName, 'utf8').toString('base64');

  return requestResponse(`${PROMPT_PREFIX}${encodedMessage}|${encodedDefault}|${encodedTitle}|${encodedIcon}\n`).then((line) => {
    if (line === 'CANCEL') return null;
    if (line.startsWith('OK:')) return Buffer.from(line.slice(3), 'base64').toString('utf8');
    return line;
  });
}

function confirm(message, title, icon) {
  const text = String(message ?? '');
  const confirmTitle = title == null ? getDefaultAlertTitle() : String(title);
  const iconName = arguments.length >= 3 ? String(icon ?? '') : '';
  const encodedMessage = Buffer.from(text, 'utf8').toString('base64');
  const encodedTitle = Buffer.from(confirmTitle, 'utf8').toString('base64');
  const encodedIcon = Buffer.from(iconName, 'utf8').toString('base64');
  return requestResponse(`${CONFIRM_PREFIX}${encodedMessage}|${encodedTitle}|${encodedIcon}\n`).then((line) => line === 'OK');
}

function getFileName() {
  return requestResponse(`${GETFILE_PREFIX}\n`).then((line) => {
    if (!line.startsWith('OK:')) return '';
    return Buffer.from(line.slice(3), 'base64').toString('utf8');
  });
}

function openFile(fullPathFileName) {
  const encoded = Buffer.from(String(fullPathFileName ?? ''), 'utf8').toString('base64');
  return requestResponse(OPENFILE_PREFIX + encoded + '\n').then((line) => line === 'OK');
}

function openFileInBackground(fullPathFileName) {
  const encoded = Buffer.from(String(fullPathFileName ?? ''), 'utf8').toString('base64');
  return requestResponse(OPENFILEINBACKGROUND_PREFIX + encoded + '\n').then((line) => line === 'OK');
}

function saveFile() {
  return requestResponse(`${SAVEFILE_PREFIX}\n`).then((line) => line === 'OK');
}

function getOpenFiles() {
  return requestResponse(`${GETOPENFILES_PREFIX}\n`).then((response) => {
    if (!response.startsWith('OK:')) return [];
    const payload = response.slice(3);
    return payload ? payload.split('|').map((path) => Buffer.from(path, 'base64').toString('utf8')) : [];
  });
}

function closeFile(fullPathFileName) {
  const encoded = Buffer.from(String(fullPathFileName ?? ''), 'utf8').toString('base64');
  return requestResponse(`${CLOSEFILE_PREFIX}${encoded}\n`).then((line) => line === 'OK');
}

function getCursor() {
  return requestResponse(`${GETCURSOR_PREFIX}\n`).then((line) => {
    if (!line.startsWith('OK:')) return { line: 0, column: 0 };
    const [lineNumber, columnNumber] = line.slice(3).split(',').map(Number);
    return { line: lineNumber, column: columnNumber };
  });
}

function setCursor(line, column) {
  return requestResponse(`${SETCURSOR_PREFIX}${line},${column}\n`).then((response) => response === 'OK');
}

function getScrollLine() {
  return requestResponse(`${GETSCROLLLINE_PREFIX}\n`).then((response) => {
    if (!response.startsWith('OK:')) return 0;
    const line = Number(response.slice(3));
    return Number.isInteger(line) && line >= 0 ? line : 0;
  });
}

function setScrollLine(line) {
  return requestResponse(`${SETSCROLLLINE_PREFIX}${line}\n`).then((response) => response === 'OK');
}

function getLine() {
  return requestResponse(`${GETLINE_PREFIX}\n`).then((line) => line.startsWith('OK:') ? Buffer.from(line.slice(3), 'base64').toString('utf8') : '');
}

function setLine(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponse(`${SETLINE_PREFIX}${encoded}\n`).then((line) => line === 'OK');
}

function getSelection() {
  return requestResponse(`${GETSELECTION_PREFIX}\n`).then((line) => line.startsWith('OK:') ? Buffer.from(line.slice(3), 'base64').toString('utf8') : '');
}

function setSelection(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponse(`${SETSELECTION_PREFIX}${encoded}\n`).then((line) => line === 'OK');
}

function hasSelection() {
  return requestResponse(`${HASSELECTION_PREFIX}\n`).then((line) => line === 'OK:1');
}

function getText() {
  return requestResponse(`${GETTEXT_PREFIX}\n`).then((line) => line.startsWith('OK:') ? Buffer.from(line.slice(3), 'base64').toString('utf8') : '');
}

function setText(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponse(`${SETTEXT_PREFIX}${encoded}\n`).then((line) => line === 'OK');
}

function clipboardMode(format) {
  if (format == null) return '';
  if (typeof format !== 'string') return null;
  const normalized = format.trim().toUpperCase();
  if (!normalized || normalized === 'TEXT') return '';
  return normalized === 'HTML' ? 'HTML' : null;
}

function getClipBoard(format) {
  const mode = clipboardMode(format);
  if (mode === null) return Promise.resolve('');
  return requestResponse(`${GETCLIPBOARD_PREFIX}${mode}\n`).then((line) => line.startsWith('OK:') ? Buffer.from(line.slice(3), 'base64').toString('utf8') : '');
}

function setClipBoard(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponse(`${SETCLIPBOARD_PREFIX}${encoded}\n`).then((line) => line === 'OK');
}

function menuCommandPayload(commandIDOrPath) {
  if (typeof commandIDOrPath === 'number') {
    return Number.isSafeInteger(commandIDOrPath) ? String(commandIDOrPath) : 'INVALID';
  }
  if (typeof commandIDOrPath === 'string') {
    const trimmed = commandIDOrPath.trim();
    if (/^[0-9]+$/.test(trimmed)) return trimmed;
    return `PATH:${Buffer.from(commandIDOrPath, 'utf8').toString('base64')}`;
  }
  return 'INVALID';
}

function runMenu(commandIDOrPath) {
  const payload = menuCommandPayload(commandIDOrPath);
  return requestResponse(`${RUNMENUCOMMAND_PREFIX}${payload}\n`).then((line) => line === 'OK');
}

function setStatusBarText(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponse(`${SETSTATUSBARTEXT_PREFIX}${encoded}\n`).then((line) => line === 'OK');
}

function setOutputPaneTitle(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponse(`${SETOUTPUTPANETITLE_PREFIX}${encoded}\n`).then((line) => line === 'OK');
}

function syncAlert(message, title, icon) {
  const text = String(message ?? '');
  const alertTitle = title == null ? getDefaultAlertTitle() : String(title);
  const iconName = arguments.length >= 3 ? String(icon ?? '') : '';
  const encodedMessage = Buffer.from(text, 'utf8').toString('base64');
  const encodedTitle = Buffer.from(alertTitle, 'utf8').toString('base64');
  const encodedIcon = Buffer.from(iconName, 'utf8').toString('base64');
  requestResponseSync(`${ALERT_PREFIX}${encodedMessage}|${encodedTitle}|${encodedIcon}\n`);
}

function syncPrompt(message, defaultValue, title, icon) {
  const text = String(message ?? '');
  const defaultText = arguments.length >= 2 ? String(defaultValue ?? '') : '';
  const promptTitle = title == null ? getDefaultAlertTitle() : String(title);
  const iconName = arguments.length >= 4 ? String(icon ?? '') : '';
  const encodedMessage = Buffer.from(text, 'utf8').toString('base64');
  const encodedDefault = Buffer.from(defaultText, 'utf8').toString('base64');
  const encodedTitle = Buffer.from(promptTitle, 'utf8').toString('base64');
  const encodedIcon = Buffer.from(iconName, 'utf8').toString('base64');
  const line = requestResponseSync(`${PROMPT_PREFIX}${encodedMessage}|${encodedDefault}|${encodedTitle}|${encodedIcon}\n`);
  if (line === 'CANCEL') return null;
  if (line.startsWith('OK:')) return Buffer.from(line.slice(3), 'base64').toString('utf8');
  return line;
}

function syncConfirm(message, title, icon) {
  const text = String(message ?? '');
  const confirmTitle = title == null ? getDefaultAlertTitle() : String(title);
  const iconName = arguments.length >= 3 ? String(icon ?? '') : '';
  const encodedMessage = Buffer.from(text, 'utf8').toString('base64');
  const encodedTitle = Buffer.from(confirmTitle, 'utf8').toString('base64');
  const encodedIcon = Buffer.from(iconName, 'utf8').toString('base64');
  return requestResponseSync(`${CONFIRM_PREFIX}${encodedMessage}|${encodedTitle}|${encodedIcon}\n`) === 'OK';
}

function syncGetFileName() {
  const line = requestResponseSync(`${GETFILE_PREFIX}\n`);
  return line.startsWith('OK:') ? Buffer.from(line.slice(3), 'base64').toString('utf8') : '';
}

function syncOpenFile(fullPathFileName) {
  const encoded = Buffer.from(String(fullPathFileName ?? ''), 'utf8').toString('base64');
  return requestResponseSync(OPENFILE_PREFIX + encoded + '\n') === 'OK';
}

function syncOpenFileInBackground(fullPathFileName) {
  const encoded = Buffer.from(String(fullPathFileName ?? ''), 'utf8').toString('base64');
  return requestResponseSync(OPENFILEINBACKGROUND_PREFIX + encoded + '\n') === 'OK';
}

function syncSaveFile() {
  return requestResponseSync(`${SAVEFILE_PREFIX}\n`) === 'OK';
}

function syncGetOpenFiles() {
  const response = requestResponseSync(`${GETOPENFILES_PREFIX}\n`);
  if (!response.startsWith('OK:')) return [];
  const payload = response.slice(3);
  return payload ? payload.split('|').map((path) => Buffer.from(path, 'base64').toString('utf8')) : [];
}

function syncCloseFile(fullPathFileName) {
  const encoded = Buffer.from(String(fullPathFileName ?? ''), 'utf8').toString('base64');
  return requestResponseSync(`${CLOSEFILE_PREFIX}${encoded}\n`) === 'OK';
}

function syncGetCursor() {
  const line = requestResponseSync(`${GETCURSOR_PREFIX}\n`);
  if (!line.startsWith('OK:')) return { line: 0, column: 0 };
  const [lineNumber, columnNumber] = line.slice(3).split(',').map(Number);
  return { line: lineNumber, column: columnNumber };
}

function syncSetCursor(line, column) {
  return requestResponseSync(`${SETCURSOR_PREFIX}${line},${column}\n`) === 'OK';
}

function syncGetScrollLine() {
  const response = requestResponseSync(`${GETSCROLLLINE_PREFIX}\n`);
  if (!response.startsWith('OK:')) return 0;
  const line = Number(response.slice(3));
  return Number.isInteger(line) && line >= 0 ? line : 0;
}

function syncSetScrollLine(line) {
  return requestResponseSync(`${SETSCROLLLINE_PREFIX}${line}\n`) === 'OK';
}

function syncGetLine() {
  const line = requestResponseSync(`${GETLINE_PREFIX}\n`);
  return line.startsWith('OK:') ? Buffer.from(line.slice(3), 'base64').toString('utf8') : '';
}

function syncSetLine(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponseSync(`${SETLINE_PREFIX}${encoded}\n`) === 'OK';
}

function syncGetSelection() {
  const line = requestResponseSync(`${GETSELECTION_PREFIX}\n`);
  return line.startsWith('OK:') ? Buffer.from(line.slice(3), 'base64').toString('utf8') : '';
}

function syncSetSelection(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponseSync(`${SETSELECTION_PREFIX}${encoded}\n`) === 'OK';
}

function syncHasSelection() {
  return requestResponseSync(`${HASSELECTION_PREFIX}\n`) === 'OK:1';
}

function syncGetText() {
  const line = requestResponseSync(`${GETTEXT_PREFIX}\n`);
  return line.startsWith('OK:') ? Buffer.from(line.slice(3), 'base64').toString('utf8') : '';
}

function syncSetText(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponseSync(`${SETTEXT_PREFIX}${encoded}\n`) === 'OK';
}

function syncGetClipBoard(format) {
  const mode = clipboardMode(format);
  if (mode === null) return '';
  const line = requestResponseSync(`${GETCLIPBOARD_PREFIX}${mode}\n`);
  return line.startsWith('OK:') ? Buffer.from(line.slice(3), 'base64').toString('utf8') : '';
}

function syncSetClipBoard(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponseSync(`${SETCLIPBOARD_PREFIX}${encoded}\n`) === 'OK';
}

function syncRunMenu(commandIDOrPath) {
  const payload = menuCommandPayload(commandIDOrPath);
  return requestResponseSync(`${RUNMENUCOMMAND_PREFIX}${payload}\n`) === 'OK';
}

function syncSetStatusBarText(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponseSync(`${SETSTATUSBARTEXT_PREFIX}${encoded}\n`) === 'OK';
}

function syncSetOutputPaneTitle(text) {
  const encoded = Buffer.from(String(text ?? ''), 'utf8').toString('base64');
  return requestResponseSync(`${SETOUTPUTPANETITLE_PREFIX}${encoded}\n`) === 'OK';
}

module.exports = {
  alert: syncAlert,
  confirm: syncConfirm,
  prompt: syncPrompt,
  getFileName: syncGetFileName,
  saveFile: syncSaveFile,
  getOpenFiles: syncGetOpenFiles,
  closeFile: syncCloseFile,
  openFile: syncOpenFile,
  openFileInBackground: syncOpenFileInBackground,
  getCursor: syncGetCursor,
  setCursor: syncSetCursor,
  getScrollLine: syncGetScrollLine,
  setScrollLine: syncSetScrollLine,
  getLine: syncGetLine,
  setLine: syncSetLine,
  getSelection: syncGetSelection,
  setSelection: syncSetSelection,
  hasSelection: syncHasSelection,
  getText: syncGetText,
  setText: syncSetText,
  getClipBoard: syncGetClipBoard,
  setClipBoard: syncSetClipBoard,
  runMenu: syncRunMenu,
  setStatusBarText: syncSetStatusBarText,
  setOutputPaneTitle: syncSetOutputPaneTitle,
  promises: {
    alert: writeAlert,
    confirm,
    prompt,
    getFileName,
    saveFile,
    getOpenFiles,
    closeFile,
    openFile,
    openFileInBackground,
    getCursor,
    setCursor,
    getScrollLine,
    setScrollLine,
    getLine,
    setLine,
    getSelection,
    setSelection,
    hasSelection,
    getText,
    setText,
    getClipBoard,
    setClipBoard,
    runMenu,
    setStatusBarText,
    setOutputPaneTitle
  }
};
