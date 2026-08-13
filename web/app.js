"use strict";

const CODE2SCAN = {
  KeyA:4, KeyB:5, KeyC:6, KeyD:7, KeyE:8, KeyF:9, KeyG:10, KeyH:11, KeyI:12,
  KeyJ:13, KeyK:14, KeyL:15, KeyM:16, KeyN:17, KeyO:18, KeyP:19, KeyQ:20,
  KeyR:21, KeyS:22, KeyT:23, KeyU:24, KeyV:25, KeyW:26, KeyX:27, KeyY:28, KeyZ:29,
  Digit1:30, Digit2:31, Digit3:32, Digit4:33, Digit5:34, Digit6:35, Digit7:36,
  Digit8:37, Digit9:38, Digit0:39,
  Enter:40, Escape:41, Backspace:42, Tab:43, Space:44,
  Minus:45, Equal:46, BracketLeft:47, BracketRight:48, Backslash:49,
  Semicolon:51, Quote:52, Backquote:53, Comma:54, Period:55, Slash:56,
  CapsLock:57, F1:58, F2:59, F3:60, F4:61, F5:62,
  Home:74, Delete:76, ArrowRight:79, ArrowLeft:80, ArrowDown:81, ArrowUp:82,
  NumpadEnter:88, Numpad1:89, Numpad2:90, Numpad3:91, Numpad4:92,
  Numpad5:93, Numpad6:94, Numpad7:95, Numpad8:96, Numpad9:97,
  Numpad0:98, NumpadDecimal:99,
  ControlLeft:224, ShiftLeft:225, AltLeft:226,
  ControlRight:228, ShiftRight:229, AltRight:230,
};

const $ = id => document.getElementById(id);
const canvas = $("screen");
const screenFrame = $("screenFrame");
const statusEl = $("status");
const toastEl = $("toast");
const ledPowerEl = $("ledPower");
const ledAEl = $("ledA");
const ledBEl = $("ledB");
const pcwDriveLedAEl = $("pcwDriveLedA");
const pcwDriveLedBEl = $("pcwDriveLedB");
const ledInputEl = $("ledInput");
const ledAudioEl = $("ledAudio");
const expansionButtonEl = $("expansion");
const expansionPanelEl = $("expansionPanel");
const expansionBackdropEl = $("expansionBackdrop");
const expansionCloseEl = $("expansionClose");
const dksoundToggleEl = $("dksoundToggle");
const dksoundStateEl = $("dksoundState");
const perryfiToggleEl = $("perryfiToggle");
const perryfiStateEl = $("perryfiState");
const perryfiModeEls = [...document.querySelectorAll('input[name="perryfiMode"]')];
const perryfiEndpointEl = $("perryfiEndpoint");
const perryfiRelayStateEl = $("perryfiRelayState");
const perryfiRelayStateWrapEl = perryfiRelayStateEl.closest(".relay-state");
const perryfiBridge = globalThis.JS1985PerryfiBridge;
const ctx = canvas.getContext("2d");
const W = 720;
const H = 256;
const VW = 720;
const VH = 512;

const offscreen = document.createElement("canvas");
offscreen.width = W;
offscreen.height = H;
const offctx = offscreen.getContext("2d");
const image = offctx.createImageData(W, H);
let pixelSharp = false;
let toastTimer = 0;
let inputLedTimer = 0;

function setStatus(message) {
  statusEl.textContent = message;
}

function showToast(message) {
  toastEl.textContent = message;
  toastEl.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toastEl.classList.remove("show"), 3200);
}

const DKSOUND_STORAGE_KEY = "javascript1985.expansion.dksound";
const PERRYFI_STORAGE_KEY = "javascript1985.expansion.perryfi";
const PERRYFI_MODE_STORAGE_KEY = "javascript1985.expansion.perryfiMode";
const PERRYFI_ENDPOINT_STORAGE_KEY = "javascript1985.expansion.perryfiEndpoint";
let dksoundEnabled = false;
let perryfiEnabled = false;
let perryfiMode = 0;
let perryfiEndpoint = perryfiBridge ? perryfiBridge.endpoint : "";
let applyDksoundHardware = () => {};
let applyPerryfiHardware = () => {};

try {
  dksoundEnabled = localStorage.getItem(DKSOUND_STORAGE_KEY) === "true";
  perryfiEnabled = localStorage.getItem(PERRYFI_STORAGE_KEY) === "true";
  perryfiMode = localStorage.getItem(PERRYFI_MODE_STORAGE_KEY) === "1" ? 1 : 0;
  const storedEndpoint = localStorage.getItem(PERRYFI_ENDPOINT_STORAGE_KEY);
  if (storedEndpoint !== null) perryfiEndpoint = storedEndpoint;
} catch (_) {
  // Keep optional hardware disconnected when storage is unavailable.
}

function updateExpansionIndicator() {
  expansionButtonEl.classList.toggle(
    "has-expansion", dksoundEnabled || perryfiEnabled
  );
}

function updateDksoundUi() {
  dksoundToggleEl.checked = dksoundEnabled;
  dksoundStateEl.textContent = dksoundEnabled ? "Enabled" : "Disabled";
  updateExpansionIndicator();
}

function setDksoundEnabled(enabled, persist = true, announce = false) {
  dksoundEnabled = Boolean(enabled);
  updateDksoundUi();
  applyDksoundHardware(dksoundEnabled);
  if (persist) {
    try {
      localStorage.setItem(DKSOUND_STORAGE_KEY, String(dksoundEnabled));
    } catch (_) {}
  }
  if (announce)
    showToast("DK'sound " + (dksoundEnabled ? "enabled" : "disabled"));
}

function updatePerryfiUi() {
  perryfiToggleEl.checked = perryfiEnabled;
  perryfiStateEl.textContent = perryfiEnabled ? "Enabled" : "Disabled";
  for (const input of perryfiModeEls)
    input.checked = Number(input.value) === perryfiMode;
  updateExpansionIndicator();
}

function setPerryfiEnabled(enabled, persist = true, announce = false) {
  perryfiEnabled = Boolean(enabled);
  updatePerryfiUi();
  applyPerryfiHardware(perryfiEnabled, perryfiMode);
  if (persist) {
    try {
      localStorage.setItem(PERRYFI_STORAGE_KEY, String(perryfiEnabled));
    } catch (_) {}
  }
  if (announce)
    showToast("PerryFi " + (perryfiEnabled ? "enabled" : "disabled"));
}

function setPerryfiMode(mode, persist = true, announce = false) {
  perryfiMode = Number(mode) === 1 ? 1 : 0;
  updatePerryfiUi();
  applyPerryfiHardware(perryfiEnabled, perryfiMode);
  if (persist) {
    try {
      localStorage.setItem(PERRYFI_MODE_STORAGE_KEY, String(perryfiMode));
    } catch (_) {}
  }
  if (announce)
    showToast("PerryFi mode: " + (perryfiMode ? "TCP/IP" : "AT Hayes"));
}

function updatePerryfiRelayStatus(status, detail = "") {
  const labels = {
    disabled: "Relay disabled",
    connecting: "Relay connecting",
    online: "Relay online",
    offline: "Relay offline",
    error: "Relay error",
  };
  perryfiRelayStateWrapEl.dataset.state = status;
  perryfiRelayStateEl.textContent = labels[status] || "Relay offline";
  perryfiRelayStateEl.title = detail;
}

function applyPerryfiEndpoint(value, persist = true) {
  perryfiEndpoint = value.trim();
  const accepted = perryfiBridge && perryfiBridge.setEndpoint(perryfiEndpoint);
  perryfiEndpointEl.setAttribute("aria-invalid", String(!accepted));
  if (accepted && persist) {
    try {
      localStorage.setItem(PERRYFI_ENDPOINT_STORAGE_KEY, perryfiEndpoint);
    } catch (_) {}
  }
  return accepted;
}

function expansionFocusableElements() {
  return [...expansionPanelEl.querySelectorAll(
    'button:not([disabled]), input:not([disabled]), select:not([disabled]), [tabindex]:not([tabindex="-1"])'
  )];
}

function setExpansionPanelOpen(open, restoreFocus = true) {
  const isOpen = Boolean(open);
  document.body.classList.toggle("expansion-open", isOpen);
  expansionButtonEl.setAttribute("aria-expanded", String(isOpen));
  expansionPanelEl.setAttribute("aria-hidden", String(!isOpen));
  expansionPanelEl.inert = !isOpen;
  if (isOpen) {
    setThemeMenu(false);
    requestAnimationFrame(() => expansionCloseEl.focus());
  } else if (restoreFocus) {
    expansionButtonEl.focus();
  }
}

expansionButtonEl.addEventListener("click", () => {
  setExpansionPanelOpen(
    expansionButtonEl.getAttribute("aria-expanded") !== "true"
  );
});
expansionCloseEl.addEventListener("click", () => setExpansionPanelOpen(false));
expansionBackdropEl.addEventListener("click", () => setExpansionPanelOpen(false));
expansionPanelEl.addEventListener("keydown", event => {
  if (event.key !== "Tab") return;
  const focusable = expansionFocusableElements();
  if (!focusable.length) return;
  const first = focusable[0];
  const last = focusable[focusable.length - 1];
  if (event.shiftKey && document.activeElement === first) {
    event.preventDefault();
    last.focus();
  } else if (!event.shiftKey && document.activeElement === last) {
    event.preventDefault();
    first.focus();
  }
});
document.addEventListener("keydown", event => {
  if (event.key === "Escape" &&
      expansionButtonEl.getAttribute("aria-expanded") === "true") {
    event.preventDefault();
    setExpansionPanelOpen(false);
  }
});
dksoundToggleEl.addEventListener("change", () => {
  setDksoundEnabled(dksoundToggleEl.checked, true, true);
});
perryfiToggleEl.addEventListener("change", () => {
  setPerryfiEnabled(perryfiToggleEl.checked, true, true);
});
for (const input of perryfiModeEls) {
  input.addEventListener("change", () => {
    if (input.checked) setPerryfiMode(input.value, true, true);
  });
}
perryfiEndpointEl.addEventListener("change", () => {
  if (!applyPerryfiEndpoint(perryfiEndpointEl.value, true))
    showToast("Invalid PerryFi relay endpoint");
});

try {
  const queryEndpoint = new URLSearchParams(window.location.search).get("perryfiRelay");
  if (queryEndpoint !== null) perryfiEndpoint = queryEndpoint;
} catch (_) {}
perryfiEndpointEl.value = perryfiEndpoint;
applyPerryfiEndpoint(perryfiEndpoint, false);
if (perryfiBridge) perryfiBridge.onStatus(updatePerryfiRelayStatus);
setExpansionPanelOpen(false, false);
updateDksoundUi();
updatePerryfiUi();

const THEMES = {
  "pcw8256": "PCW8256",
  "retro-crt": "Retro CRT",
  "sapporo": "Sapporo",
  "sapporo-dark": "Sapporo Dark",
};
const THEME_STORAGE_KEY = "javascript1985.theme";
const themePickerEl = document.querySelector(".theme-picker");
const themeButtonEl = $("themeButton");
const themeMenuEl = $("themeMenu");
const themeNameEl = $("themeName");
let releaseVirtualKeyboard = () => {};

function resolveTheme(theme) {
  if (typeof theme !== "string") return null;
  const normalized = theme.trim().toLowerCase();
  for (const [id, label] of Object.entries(THEMES)) {
    if (normalized === id.toLowerCase() || normalized === label.toLowerCase())
      return id;
  }
  return null;
}

function setThemeMenu(open) {
  themeMenuEl.hidden = !open;
  themeButtonEl.setAttribute("aria-expanded", String(open));
}

function applyTheme(theme, persist = true) {
  const selected = resolveTheme(theme) || "pcw8256";
  if (selected !== "pcw8256") releaseVirtualKeyboard();
  document.documentElement.dataset.theme = selected;
  themeNameEl.textContent = THEMES[selected];
  for (const option of themeMenuEl.querySelectorAll("[data-theme]"))
    option.setAttribute("aria-checked", String(option.dataset.theme === selected));
  if (persist) {
    try {
      localStorage.setItem(THEME_STORAGE_KEY, selected);
    } catch (_) {}
  }
}

let savedTheme = "pcw8256";
try {
  savedTheme = localStorage.getItem(THEME_STORAGE_KEY) || "pcw8256";
} catch (_) {
  // Keep the PCW8256 theme when storage access is unavailable.
}
const requestedTheme = new URLSearchParams(window.location.search).get("theme");
applyTheme(requestedTheme || savedTheme, false);

themeButtonEl.addEventListener("click", event => {
  event.stopPropagation();
  setThemeMenu(themeMenuEl.hidden);
});
themeMenuEl.addEventListener("click", event => {
  const option = event.target.closest("[data-theme]");
  if (!option) return;
  applyTheme(option.dataset.theme);
  setThemeMenu(false);
  themeButtonEl.focus();
  showToast(THEMES[option.dataset.theme] + " theme selected");
});
themePickerEl.addEventListener("keydown", event => {
  if (event.key === "Escape") {
    setThemeMenu(false);
    themeButtonEl.focus();
  }
});
document.addEventListener("click", event => {
  if (!themePickerEl.contains(event.target)) setThemeMenu(false);
});

const pcwKeyboardEl = document.querySelector(".pcw8256-keyboard");
const pcwKeyboardKeysEl = $("pcwKeyboardKeys");
const pcwKeyboardToggleEl = $("pcwKeyboardToggle");

function setPcwKeyboardOpen(open) {
  pcwKeyboardEl.dataset.keyboardOpen = String(open);
  pcwKeyboardKeysEl.hidden = !open;
  pcwKeyboardToggleEl.setAttribute("aria-expanded", String(open));
  pcwKeyboardToggleEl.textContent = open ? "Hide keyboard" : "Show keyboard";
}

pcwKeyboardToggleEl.addEventListener("click", () => {
  setPcwKeyboardOpen(pcwKeyboardKeysEl.hidden);
});
setPcwKeyboardOpen(false);

function pulseInputLed() {
  ledInputEl.classList.add("on");
  clearTimeout(inputLedTimer);
  inputLedTimer = setTimeout(() => ledInputEl.classList.remove("on"), 120);
}

function setScreenScale(value) {
  const scale = Number(value);
  document.documentElement.style.setProperty("--screen-scale", String(scale / 100));
  $("screenScale").value = String(scale);
  $("scaleValue").textContent = scale + "%";
  const rotation = -115 + ((scale - 70) / 30) * 230;
  $("sizeNeedle").style.transform = "rotate(" + rotation + "deg)";
}

function updatePixelMode() {
  pixelSharp = $("pixelToggle").checked;
  canvas.style.imageRendering = pixelSharp ? "pixelated" : "auto";
  updateScreenModeReadout();
}

function updateScreenModeReadout() {
  $("screenMode").textContent = "720 x 512 / " +
    (pixelSharp ? "Sharp" : "Smooth");
}

$("screenScale").addEventListener("input", event => setScreenScale(event.target.value));
$("fitScreen").addEventListener("click", () => {
  setScreenScale(100);
  showToast("Display fitted to the receiver");
});
$("pixelToggle").addEventListener("change", updatePixelMode);
$("fullscreen").addEventListener("click", async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await screenFrame.requestFullscreen();
  } catch (error) {
    setStatus("Fullscreen unavailable: " + error.message);
  }
});
setScreenScale(100);
updatePixelMode();

create1985().then(m => {
  if (m._poc_init() !== 0) {
    setStatus("Emulator initialization failed");
    return;
  }

  ledPowerEl.classList.add("on");
  setStatus("PCW 8256 booting - click the display for keyboard focus");

  const framebuffer = m._poc_pixels();
  const modelEl = $("model");
  const resetEl = $("reset");
  const diskfileAEl = $("diskfileA");
  const disknameAEl = $("disknameA");
  const diskEjectAEl = $("diskEjectA");
  const diskfileBEl = $("diskfileB");
  const disknameBEl = $("disknameB");
  const diskEjectBEl = $("diskEjectB");
  const joytoggleEl = $("joytoggle");
  const joystatusEl = $("joystatus");

  let currentModel = 0;
  let audioCtx = null;
  let audioState = null;
  let nextAudioStart = 0;
  let prevGamepad = null;
  let joyEnabled = true;
  let driveActivityState = -1;
  const heldKeys = new Set();
  const virtualKeys = new Set();
  const latchedVirtualModifiers = new Set();

  applyDksoundHardware = enabled => {
    const actual = Boolean(m._poc_set_dksound(enabled ? 1 : 0));
    if (actual !== dksoundEnabled) {
      dksoundEnabled = actual;
      updateDksoundUi();
    }
    m._poc_audio_reset();
    if (audioCtx) nextAudioStart = audioCtx.currentTime + 0.3;
  };
  applyDksoundHardware(dksoundEnabled);

  perryfiBridge.attachModule(m);
  applyPerryfiHardware = (enabled, mode) => {
    const expectedEnabled = Boolean(enabled);
    const expectedMode = Number(mode) === 1 ? 1 : 0;
    let actualEnabled = Boolean(m._poc_perryfi_enabled());
    let actualMode = m._poc_perryfi_mode();
    if (actualEnabled !== expectedEnabled || actualMode !== expectedMode) {
      m._poc_set_perryfi(expectedEnabled ? 1 : 0, expectedMode);
      actualEnabled = Boolean(m._poc_perryfi_enabled());
      actualMode = m._poc_perryfi_mode();
    }
    if (actualEnabled !== perryfiEnabled || actualMode !== perryfiMode) {
      perryfiEnabled = actualEnabled;
      perryfiMode = actualMode;
      updatePerryfiUi();
    }
  };
  applyPerryfiHardware(perryfiEnabled, perryfiMode);

  function pressVirtualKey(scancode) {
    if (virtualKeys.has(scancode)) return;
    const alreadyPressed = heldKeys.has(scancode);
    virtualKeys.add(scancode);
    if (!alreadyPressed) m._poc_key(scancode, 1);
  }

  function releaseVirtualKey(scancode) {
    if (!virtualKeys.delete(scancode)) return;
    if (!heldKeys.has(scancode)) m._poc_key(scancode, 0);
  }

  function setModifierUi(scancode, active) {
    for (const button of pcwKeyboardKeysEl.querySelectorAll(
      `[data-modifier][data-scancode="${scancode}"]`
    )) {
      button.classList.toggle("latched", active);
      button.setAttribute("aria-pressed", String(active));
    }
  }

  function releaseLatchedModifiers() {
    for (const scancode of latchedVirtualModifiers) {
      releaseVirtualKey(scancode);
      setModifierUi(scancode, false);
    }
    latchedVirtualModifiers.clear();
  }

  function toggleVirtualModifier(scancode) {
    if (latchedVirtualModifiers.delete(scancode)) {
      releaseVirtualKey(scancode);
      setModifierUi(scancode, false);
    } else {
      latchedVirtualModifiers.add(scancode);
      pressVirtualKey(scancode);
      setModifierUi(scancode, true);
    }
  }

  function releaseAllVirtualKeys() {
    for (const scancode of [...virtualKeys]) releaseVirtualKey(scancode);
    latchedVirtualModifiers.clear();
    for (const button of pcwKeyboardKeysEl.querySelectorAll("[data-scancode]")) {
      button.classList.remove("active", "latched");
      if (button.hasAttribute("data-modifier"))
        button.setAttribute("aria-pressed", "false");
    }
  }

  function virtualKeyButton(target) {
    return target.closest("button[data-scancode]");
  }

  pcwKeyboardKeysEl.addEventListener("pointerdown", event => {
    const button = virtualKeyButton(event.target);
    if (!button) return;
    event.preventDefault();
    startAudio();
    const scancode = Number(button.dataset.scancode);
    if (button.hasAttribute("data-modifier")) {
      toggleVirtualModifier(scancode);
    } else {
      pressVirtualKey(scancode);
      button.classList.add("active");
      button.setPointerCapture(event.pointerId);
    }
    pulseInputLed();
  });

  function finishVirtualPointer(event) {
    const button = virtualKeyButton(event.target);
    if (!button || button.hasAttribute("data-modifier")) return;
    releaseVirtualKey(Number(button.dataset.scancode));
    button.classList.remove("active");
    releaseLatchedModifiers();
  }

  pcwKeyboardKeysEl.addEventListener("pointerup", finishVirtualPointer);
  pcwKeyboardKeysEl.addEventListener("pointercancel", finishVirtualPointer);
  pcwKeyboardKeysEl.addEventListener("lostpointercapture", finishVirtualPointer);
  pcwKeyboardKeysEl.addEventListener("click", event => {
    if (event.detail !== 0) return;
    const button = virtualKeyButton(event.target);
    if (!button) return;
    startAudio();
    const scancode = Number(button.dataset.scancode);
    if (button.hasAttribute("data-modifier")) {
      toggleVirtualModifier(scancode);
    } else {
      pressVirtualKey(scancode);
      button.classList.add("active");
      setTimeout(() => {
        releaseVirtualKey(scancode);
        button.classList.remove("active");
        releaseLatchedModifiers();
      }, 90);
    }
    pulseInputLed();
  });
  pcwKeyboardToggleEl.addEventListener("click", () => {
    if (pcwKeyboardKeysEl.hidden) releaseAllVirtualKeys();
  });
  releaseVirtualKeyboard = releaseAllVirtualKeys;

  function clearDiskAUi() {
    disknameAEl.textContent = "No disk loaded";
    diskEjectAEl.disabled = true;
    diskfileAEl.value = "";
  }

  function clearDiskBUi() {
    disknameBEl.textContent = "No disk loaded";
    diskEjectBEl.disabled = true;
    diskfileBEl.value = "";
  }

  function releaseAllJoy() {
    for (let column = 0; column < 6; column++)
      m._poc_joy(column, 0);
    prevGamepad = null;
  }

  function reinit(model) {
    if (m._poc_init_model(model) !== 0) {
      setStatus("Machine initialization failed");
      return false;
    }
    currentModel = model;
    modelEl.value = String(model);
    applyDksoundHardware(dksoundEnabled);
    applyPerryfiHardware(perryfiEnabled, perryfiMode);
    releaseAllJoy();
    clearDiskAUi();
    clearDiskBUi();
    setStatus("Machine reset");
    return true;
  }

  modelEl.addEventListener("change", () => {
    const model = Number(modelEl.value);
    if (reinit(model)) {
      const names = {0: "PCW 8256", 1: "PCW 8512", 2: "PCW 9512"};
      showToast(names[model] + " selected");
    }
  });

  resetEl.addEventListener("click", () => {
    m._poc_reset();
    m._poc_audio_reset();
    if (audioCtx) nextAudioStart = audioCtx.currentTime + 0.3;
    releaseAllJoy();
    setStatus("Warm reset complete");
    showToast("PCW reset");
    canvas.focus();
  });

  function mountDisk(data, name, path, drive) {
    m.FS.writeFile(path, data);
    const func = drive === "b" ? "poc_load_disk_b" : "poc_load_disk";
    const rc = m.ccall(func, "number", ["string"], [path]);
    if (rc !== 0) throw new Error("unsupported or damaged disk image");
    return name;
  }

  async function loadDiskFile(file, drive) {
    if (!file) return;
    try {
      const data = new Uint8Array(await file.arrayBuffer());
      const path = drive === "b" ? "/disk-b.dsk" : "/disk-a.dsk";
      const name = mountDisk(data, file.name, path, drive);
      if (drive === "b") {
        disknameBEl.textContent = name;
        diskEjectBEl.disabled = false;
        setStatus("Drive B: " + name);
        showToast("Disk loaded into Drive B");
      } else {
        disknameAEl.textContent = name;
        diskEjectAEl.disabled = false;
        setStatus("Drive A: " + name);
        showToast("Disk loaded into Drive A");
      }
    } catch (error) {
      setStatus("Disk load failed: " + error.message);
      showToast("Could not load " + file.name);
    }
  }

  diskfileAEl.addEventListener("change", () => loadDiskFile(diskfileAEl.files[0], "a"));
  diskEjectAEl.addEventListener("click", () => {
    m._poc_eject_disk();
    clearDiskAUi();
    setStatus("Drive A ejected");
  });
  diskfileBEl.addEventListener("change", () => loadDiskFile(diskfileBEl.files[0], "b"));
  diskEjectBEl.addEventListener("click", () => {
    m._poc_eject_disk_b();
    clearDiskBUi();
    setStatus("Drive B ejected");
  });

  async function fetchServerMedia(url, kind) {
    const name = JS1985Media.filenameFromUrl(url, kind);
    setStatus("Fetching " + kind + ": " + name);
    const response = await fetch(url);
    if (!response.ok)
      throw new Error(kind + " request returned HTTP " + response.status);
    const data = new Uint8Array(await response.arrayBuffer());
    if (!data.byteLength) throw new Error(kind + " response was empty");
    return { data, name };
  }

  async function bootstrapServerMedia() {
    let media;
    try {
      media = JS1985Media.parseStartupMedia(
        window.location.search,
        document.baseURI
      );
    } catch (error) {
      setStatus("Media URL error: " + error.message);
      showToast("Invalid server media URL");
      return;
    }
    if (!media.disk && !media.diskB) return;

    try {
      const [diskA, diskB] = await Promise.all([
        media.disk ? fetchServerMedia(media.disk, "Drive A disk") : null,
        media.diskB ? fetchServerMedia(media.diskB, "Drive B disk") : null,
      ]);

      if (diskB) {
        mountDisk(diskB.data, diskB.name, "/server-disk-b.dsk", "b");
        disknameBEl.textContent = diskB.name;
        diskEjectBEl.disabled = false;
      }
      if (diskA) {
        mountDisk(diskA.data, diskA.name, "/server-disk-a.dsk", "a");
        disknameAEl.textContent = diskA.name;
        diskEjectAEl.disabled = false;

        m._poc_reset();
        m._poc_audio_reset();
        if (audioCtx) nextAudioStart = audioCtx.currentTime + 0.3;
        releaseAllJoy();

        if (!media.autorun) {
          setStatus("Drive A: " + diskA.name + " - booting");
          showToast("Booting " + diskA.name + " from Drive A");
          return;
        }
        const rc = m.ccall(
          "poc_autorun",
          "number",
          ["string", "number"],
          [media.autorun, 120]
        );
        if (rc !== 0) throw new Error("invalid autorun command");
        setStatus(
          "Drive A: " + diskA.name + " - autorun " + media.autorun + " armed"
        );
        showToast("Autorun " + media.autorun + " armed");
      } else {
        setStatus("Drive B: " + diskB.name);
        showToast("Disk loaded into Drive B");
      }
    } catch (error) {
      setStatus("Server media failed: " + error.message);
      showToast("Could not load server media");
    }
  }

  const AUDIO_CHUNK = 2048;
  function startAudio() {
    if (audioCtx) {
      audioCtx.resume();
      return;
    }
    audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    m._poc_audio_reset();
    audioState = { ringPtr: m._poc_audio_buffer(), ringSize: 44100 * 4 };
    nextAudioStart = audioCtx.currentTime + 0.3;
    audioCtx.resume().then(() => ledAudioEl.classList.add("on"));
  }

  function scheduleAudio() {
    if (!audioCtx || audioCtx.state !== "running") return;
    if (nextAudioStart < audioCtx.currentTime + 0.05)
      nextAudioStart = audioCtx.currentTime + 0.05;
    while (nextAudioStart - audioCtx.currentTime < 0.25) {
      const available = m._poc_audio_avail();
      const frames = Math.min(AUDIO_CHUNK, available);
      if (frames === 0) break;
      const readPosition = m._poc_audio_read_pos();
      const samples = new Int16Array(
        m.HEAPU8.buffer,
        audioState.ringPtr,
        audioState.ringSize
      );
      const buffer = audioCtx.createBuffer(1, AUDIO_CHUNK, 44100);
      const channel = buffer.getChannelData(0);
      for (let i = 0; i < frames; i++) {
        channel[i] = samples[(readPosition + i) % audioState.ringSize] / 32768;
      }
      m._poc_audio_advance(frames);
      const source = audioCtx.createBufferSource();
      source.buffer = buffer;
      source.connect(audioCtx.destination);
      source.start(nextAudioStart);
      nextAudioStart += AUDIO_CHUNK / 44100;
    }
  }

  window.addEventListener("pointerdown", startAudio, { once: true });

  function setJoystickEnabled(enabled) {
    joyEnabled = enabled;
    joytoggleEl.checked = enabled;
    if (!enabled) releaseAllJoy();
    joystatusEl.textContent = enabled ? "Joystick: enabled" : "Joystick: disabled";
  }

  joytoggleEl.addEventListener("change", () => {
    setJoystickEnabled(joytoggleEl.checked);
  });

  function gamepadUnavailableReason() {
    if (!window.isSecureContext)
      return "Gamepad API requires HTTPS or localhost";
    if (typeof navigator.getGamepads !== "function")
      return "Gamepad API is unavailable in this browser";
    const policy = document.permissionsPolicy || document.featurePolicy;
    if (policy && typeof policy.allowsFeature === "function" && !policy.allowsFeature("gamepad"))
      return "Gamepad API is blocked by Permissions Policy";
    return "";
  }

  function pollGamepad() {
    const unavailable = gamepadUnavailableReason();
    if (unavailable) {
      joystatusEl.textContent = "Joystick unavailable: " + unavailable;
      return;
    }
    let pads;
    try {
      pads = navigator.getGamepads();
    } catch (error) {
      joystatusEl.textContent = "Joystick unavailable: " + error.message;
      return;
    }
    let gamepad = null;
    for (const pad of pads) {
      if (pad && pad.connected) {
        gamepad = pad;
        break;
      }
    }
    if (!gamepad) {
      if (prevGamepad) releaseAllJoy();
      joystatusEl.textContent = "Joystick: no controller exposed";
      return;
    }

    const mapped = JS1985Gamepad.mapGamepad(gamepad);
    const state = mapped.state;
    const names = ["UP", "DOWN", "LEFT", "RIGHT", "FIRE1", "FIRE2"];
    if (!joyEnabled) {
      if (prevGamepad) releaseAllJoy();
      return;
    }

    if (state.some(Boolean)) {
      joystatusEl.textContent = "Joystick [" + mapped.profile + "]: " +
        names.filter((_, column) => state[column]).join(" ");
      pulseInputLed();
    } else {
      const rawButtons = [];
      const rawAxes = [];
      for (let i = 0; i < gamepad.buttons.length; i++) {
        const button = gamepad.buttons[i];
        if (button && (button.pressed || button.value > 0.5)) rawButtons.push(i);
      }
      for (let i = 0; i < gamepad.axes.length; i++) {
        if (Math.abs(gamepad.axes[i]) > 0.5) rawAxes.push(i + "=" + gamepad.axes[i].toFixed(2));
      }
      joystatusEl.textContent = rawButtons.length || rawAxes.length
        ? "Joystick raw: B " + (rawButtons.join(",") || "-") + " / A " + (rawAxes.join(",") || "-")
        : "Joystick: " + gamepad.id;
    }

    if (prevGamepad) {
      for (let column = 0; column < 6; column++) {
        if (prevGamepad[column] !== state[column]) m._poc_joy(column, state[column]);
      }
    } else {
      for (let column = 0; column < 6; column++) {
        if (state[column]) m._poc_joy(column, 1);
      }
    }
    prevGamepad = state;
  }

  window.addEventListener("gamepadconnected", event => {
    joystatusEl.textContent = "Joystick: connected " + event.gamepad.id;
    showToast("Game controller connected");
  });
  window.addEventListener("gamepaddisconnected", () => {
    releaseAllJoy();
    joystatusEl.textContent = "Joystick: disconnected";
  });
  window.addEventListener("focus", pollGamepad);
  $("joydetect").addEventListener("click", () => {
    startAudio();
    pollGamepad();
    showToast("Scanning browser game controllers");
  });
  setInterval(pollGamepad, 100);

  canvas.addEventListener("click", () => {
    canvas.focus();
    startAudio();
  });
  canvas.addEventListener("contextmenu", event => event.preventDefault());

  window.addEventListener("keydown", event => {
    const scancode = CODE2SCAN[event.code];
    if (scancode === undefined || document.activeElement !== canvas) return;
    event.preventDefault();
    startAudio();
    if (!heldKeys.has(scancode)) {
      const alreadyPressed = virtualKeys.has(scancode);
      heldKeys.add(scancode);
      if (!alreadyPressed) m._poc_key(scancode, 1);
      pulseInputLed();
    }
  });
  window.addEventListener("keyup", event => {
    const scancode = CODE2SCAN[event.code];
    if (scancode === undefined || !heldKeys.has(scancode)) return;
    event.preventDefault();
    heldKeys.delete(scancode);
    if (!virtualKeys.has(scancode)) m._poc_key(scancode, 0);
  });
  canvas.addEventListener("blur", () => {
    for (const scancode of heldKeys) {
      if (!virtualKeys.has(scancode)) m._poc_key(scancode, 0);
    }
    heldKeys.clear();
  });
  window.addEventListener("blur", releaseAllVirtualKeys);

  for (const eventName of ["dragenter", "dragover"]) {
    screenFrame.addEventListener(eventName, event => {
      event.preventDefault();
      screenFrame.classList.add("dragging");
    });
  }
  for (const eventName of ["dragleave", "drop"]) {
    screenFrame.addEventListener(eventName, event => {
      event.preventDefault();
      screenFrame.classList.remove("dragging");
    });
  }
  screenFrame.addEventListener("drop", event => {
    const file = event.dataTransfer.files[0];
    if (!file) return;
    const lowerName = file.name.toLowerCase();
    if (lowerName.endsWith(".dsk")) loadDiskFile(file, "a");
    else showToast("Use a DSK disk image");
  });

  function updateDriveLeds() {
    const activity = m._poc_disk_activity();
    if (activity === driveActivityState) return;
    driveActivityState = activity;

    const driveAActive = Boolean(activity & 0x01);
    const driveBActive = Boolean(activity & 0x02);
    ledAEl.classList.toggle("on", driveAActive);
    ledBEl.classList.toggle("on", driveBActive);
    pcwDriveLedAEl.classList.toggle("on", driveAActive);
    pcwDriveLedBEl.classList.toggle("on", driveBActive);
  }

  let lastFrame = 0;
  function frame(time) {
    while (time - lastFrame >= 20) {
      m._poc_step();
      lastFrame += 20;
      scheduleAudio();
      pollGamepad();
      updateDriveLeds();
    }

    const pixels = m.HEAPU32.subarray(framebuffer >> 2, (framebuffer >> 2) + W * H);
    for (let i = 0, destination = 0; i < W * H; i++, destination += 4) {
      const color = pixels[i];
      const red = (color >> 16) & 0xff;
      const green = (color >> 8) & 0xff;
      const blue = color & 0xff;
      image.data[destination] = red;
      image.data[destination + 1] = green;
      image.data[destination + 2] = blue;
      image.data[destination + 3] = 0xff;
    }
    offctx.putImageData(image, 0, 0);
    ctx.imageSmoothingEnabled = !pixelSharp;
    ctx.drawImage(offscreen, 0, 0, W, H, 0, 0, VW, VH);
    requestAnimationFrame(frame);
  }

  requestAnimationFrame(frame);
  bootstrapServerMedia();
}).catch(error => {
  setStatus("Failed to start: " + error);
});
