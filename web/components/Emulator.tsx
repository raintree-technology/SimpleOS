"use client";

import { type KeyboardEvent as ReactKeyboardEvent, useCallback, useEffect, useRef, useState } from "react";
import CRTFrame from "./CRTFrame";

declare global {
  interface Window {
    V86?: new (options: V86Options) => V86Instance;
  }
}

interface V86Options {
  wasm_path: string;
  memory_size: number;
  vga_memory_size: number;
  screen_container: HTMLElement;
  bios: { url: string };
  vga_bios: { url: string };
  cdrom?: { url: string };
  autostart?: boolean;
  disable_keyboard?: boolean;
  disable_mouse?: boolean;
}

interface V86Instance {
  add_listener: (event: string, callback: () => void) => void;
  restart: () => void;
  stop: () => void;
  bus: {
    send: (event: string, data: number) => void;
  };
}

type EmulatorStatus =
  | "checking"
  | "ready_to_boot"
  | "booting"
  | "running"
  | "missing_assets"
  | "error";

type AssetKey = "libv86" | "wasm" | "bios" | "vgaBios" | "iso";

interface AssetState {
  key: AssetKey;
  label: string;
  url: string;
  requiredForBoot: boolean;
  present: boolean;
}

const ASSET_DEFS: Omit<AssetState, "present">[] = [
  { key: "libv86", label: "v86 runtime", url: "/v86/libv86.js", requiredForBoot: true },
  { key: "wasm", label: "v86 WebAssembly core", url: "/v86/v86.wasm", requiredForBoot: true },
  { key: "bios", label: "SeaBIOS image", url: "/bios/seabios.bin", requiredForBoot: true },
  { key: "vgaBios", label: "VGA BIOS image", url: "/bios/vgabios.bin", requiredForBoot: true },
  { key: "iso", label: "SimpleOS ISO", url: "/os/simpleos.iso", requiredForBoot: true },
];

const DEMO_COMMANDS = [
  "make            # build simpleos.iso with a local cross-toolchain",
  "./build.sh      # build via Docker and copy the ISO into web/public/os when possible",
  "cd web && npm run dev",
];

const SHELL_COMMANDS = ["help", "ps", "ls", "echo demo ready", "clear"];
const TUI_DOTS = ["#7fcf8c", "#d8c36a", "#c96b6b"];

const AUTO_SHELL_COMBO = ["ShiftLeft", "KeyS"] as const;

async function probeAsset(def: Omit<AssetState, "present">): Promise<AssetState> {
  try {
    const response = await fetch(def.url, { method: "HEAD", cache: "no-store" });
    return { ...def, present: response.ok };
  } catch {
    return { ...def, present: false };
  }
}

export default function Emulator() {
  const screenRef = useRef<HTMLDivElement>(null);
  const emulatorRef = useRef<V86Instance | null>(null);
  const scriptRef = useRef<HTMLScriptElement | null>(null);

  const [status, setStatus] = useState<EmulatorStatus>("checking");
  const [error, setError] = useState<string | null>(null);
  const [shellCommand, setShellCommand] = useState("help");
  const [vmFocused, setVmFocused] = useState(false);
  const [assets, setAssets] = useState<AssetState[]>(
    ASSET_DEFS.map((asset) => ({ ...asset, present: false })),
  );

  const refreshAssets = useCallback(async () => {
    setStatus("checking");
    setError(null);

    const nextAssets = await Promise.all(ASSET_DEFS.map(probeAsset));
    const bootReady = nextAssets.every((asset) => !asset.requiredForBoot || asset.present);

    setAssets(nextAssets);
    setStatus(bootReady ? "ready_to_boot" : "missing_assets");
  }, []);

  useEffect(() => {
    void refreshAssets();
  }, [refreshAssets]);

  const ensureScriptLoaded = useCallback(async () => {
    if (window.V86) {
      return;
    }

    if (scriptRef.current) {
      await new Promise<void>((resolve, reject) => {
        const script = scriptRef.current;
        if (!script) {
          reject(new Error("v86 script reference lost"));
          return;
        }

        script.addEventListener("load", () => resolve(), { once: true });
        script.addEventListener("error", () => reject(new Error("Failed to load v86 library")), {
          once: true,
        });
      });
      return;
    }

    await new Promise<void>((resolve, reject) => {
      const script = document.createElement("script");
      script.src = "/v86/libv86.js";
      script.async = true;
      script.onload = () => resolve();
      script.onerror = () => reject(new Error("Failed to load v86 library"));
      scriptRef.current = script;
      document.head.appendChild(script);
    });
  }, []);

  const bootEmulator = useCallback(async () => {
    if (!screenRef.current) {
      return;
    }

    setStatus("booting");
    setError(null);

    try {
      await ensureScriptLoaded();

      if (!window.V86) {
        throw new Error("v86 runtime did not initialize");
      }

      screenRef.current.innerHTML = "";
      const canvas = document.createElement("canvas");
      const textScreen = document.createElement("div");
      canvas.setAttribute("aria-label", "SimpleOS graphical screen");
      textScreen.setAttribute("aria-label", "SimpleOS text screen");
      textScreen.style.display = "none";
      screenRef.current.append(canvas, textScreen);
      emulatorRef.current?.stop();

      emulatorRef.current = new window.V86({
        wasm_path: "/v86/v86.wasm",
        memory_size: 64 * 1024 * 1024,
        vga_memory_size: 8 * 1024 * 1024,
        screen_container: screenRef.current,
        bios: { url: "/bios/seabios.bin" },
        vga_bios: { url: "/bios/vgabios.bin" },
        cdrom: { url: `/os/simpleos.iso?v=${Date.now()}` },
        autostart: true,
        disable_keyboard: false,
        disable_mouse: false,
      });

      emulatorRef.current.add_listener("emulator-ready", () => {
        setStatus("running");
        setVmFocused(true);

        if (screenRef.current instanceof HTMLElement) {
          screenRef.current.focus();
        }
      });
    } catch (nextError) {
      emulatorRef.current?.stop();
      setError(nextError instanceof Error ? nextError.message : "Failed to start emulator");
      setStatus("error");
    }
  }, [ensureScriptLoaded]);

  const restart = useCallback(() => {
    emulatorRef.current?.restart();
  }, []);

  useEffect(() => {
    return () => {
      emulatorRef.current?.stop();
      scriptRef.current?.remove();
    };
  }, []);

  useEffect(() => {
    if (status !== "running" || !emulatorRef.current || !vmFocused) {
      return;
    }

    const scancodeMap: Record<string, number> = {
      KeyA: 0x1e,
      KeyB: 0x30,
      KeyC: 0x2e,
      KeyD: 0x20,
      KeyE: 0x12,
      KeyF: 0x21,
      KeyG: 0x22,
      KeyH: 0x23,
      KeyI: 0x17,
      KeyJ: 0x24,
      KeyK: 0x25,
      KeyL: 0x26,
      KeyM: 0x32,
      KeyN: 0x31,
      KeyO: 0x18,
      KeyP: 0x19,
      KeyQ: 0x10,
      KeyR: 0x13,
      KeyS: 0x1f,
      KeyT: 0x14,
      KeyU: 0x16,
      KeyV: 0x2f,
      KeyW: 0x11,
      KeyX: 0x2d,
      KeyY: 0x15,
      KeyZ: 0x2c,
      Digit0: 0x0b,
      Digit1: 0x02,
      Digit2: 0x03,
      Digit3: 0x04,
      Digit4: 0x05,
      Digit5: 0x06,
      Digit6: 0x07,
      Digit7: 0x08,
      Digit8: 0x09,
      Digit9: 0x0a,
      Space: 0x39,
      Enter: 0x1c,
      Backspace: 0x0e,
      Tab: 0x0f,
      Escape: 0x01,
      Minus: 0x0c,
      Equal: 0x0d,
      BracketLeft: 0x1a,
      BracketRight: 0x1b,
      Backslash: 0x2b,
      Semicolon: 0x27,
      Quote: 0x28,
      Backquote: 0x29,
      Comma: 0x33,
      Period: 0x34,
      Slash: 0x35,
      ShiftLeft: 0x2a,
      ShiftRight: 0x36,
      ControlLeft: 0x1d,
      AltLeft: 0x38,
      ArrowUp: 0xe048,
      ArrowDown: 0xe050,
      ArrowLeft: 0xe04b,
      ArrowRight: 0xe04d,
      F1: 0x3b,
      F2: 0x3c,
      F3: 0x3d,
      F4: 0x3e,
      F5: 0x3f,
      F6: 0x40,
      F7: 0x41,
      F8: 0x42,
      F9: 0x43,
      F10: 0x44,
      F11: 0x57,
      F12: 0x58,
    };

    const sendScancode = (scancode: number, released: boolean) => {
      if (scancode > 0xff) {
        emulatorRef.current?.bus.send("keyboard-code", 0xe0);
        emulatorRef.current?.bus.send(
          "keyboard-code",
          released ? (scancode & 0xff) | 0x80 : scancode & 0xff,
        );
        return;
      }

      emulatorRef.current?.bus.send("keyboard-code", released ? scancode | 0x80 : scancode);
    };

    const isEditableTarget = (target: EventTarget | null) => {
      if (!(target instanceof HTMLElement)) {
        return false;
      }

      return (
        target.tagName === "INPUT" ||
        target.tagName === "TEXTAREA" ||
        target.isContentEditable
      );
    };

    const handleKeyDown = (event: KeyboardEvent) => {
      if (isEditableTarget(event.target)) {
        return;
      }

      if (event.key === "Escape") {
        event.preventDefault();
        screenRef.current?.blur();
        setVmFocused(false);
        return;
      }

      const scancode = scancodeMap[event.code];
      if (scancode !== undefined) {
        event.preventDefault();
        sendScancode(scancode, false);
      }
    };

    const handleKeyUp = (event: KeyboardEvent) => {
      if (isEditableTarget(event.target)) {
        return;
      }

      const scancode = scancodeMap[event.code];
      if (scancode !== undefined) {
        event.preventDefault();
        sendScancode(scancode, true);
      }
    };

    window.addEventListener("keydown", handleKeyDown, { capture: true });
    window.addEventListener("keyup", handleKeyUp, { capture: true });

    return () => {
      window.removeEventListener("keydown", handleKeyDown, { capture: true });
      window.removeEventListener("keyup", handleKeyUp, { capture: true });
    };
  }, [status, vmFocused]);

  const scancodeMap: Record<string, number> = {
    KeyA: 0x1e,
    KeyB: 0x30,
    KeyC: 0x2e,
    KeyD: 0x20,
    KeyE: 0x12,
    KeyF: 0x21,
    KeyG: 0x22,
    KeyH: 0x23,
    KeyI: 0x17,
    KeyJ: 0x24,
    KeyK: 0x25,
    KeyL: 0x26,
    KeyM: 0x32,
    KeyN: 0x31,
    KeyO: 0x18,
    KeyP: 0x19,
    KeyQ: 0x10,
    KeyR: 0x13,
    KeyS: 0x1f,
    KeyT: 0x14,
    KeyU: 0x16,
    KeyV: 0x2f,
    KeyW: 0x11,
    KeyX: 0x2d,
    KeyY: 0x15,
    KeyZ: 0x2c,
    Digit0: 0x0b,
    Digit1: 0x02,
    Digit2: 0x03,
    Digit3: 0x04,
    Digit4: 0x05,
    Digit5: 0x06,
    Digit6: 0x07,
    Digit7: 0x08,
    Digit8: 0x09,
    Digit9: 0x0a,
    Space: 0x39,
    Enter: 0x1c,
    Backspace: 0x0e,
    Tab: 0x0f,
    Escape: 0x01,
    Minus: 0x0c,
    Equal: 0x0d,
    BracketLeft: 0x1a,
    BracketRight: 0x1b,
    Backslash: 0x2b,
    Semicolon: 0x27,
    Quote: 0x28,
    Backquote: 0x29,
    Comma: 0x33,
    Period: 0x34,
    Slash: 0x35,
    ShiftLeft: 0x2a,
    ShiftRight: 0x36,
    ControlLeft: 0x1d,
    AltLeft: 0x38,
    ArrowUp: 0xe048,
    ArrowDown: 0xe050,
    ArrowLeft: 0xe04b,
    ArrowRight: 0xe04d,
    F1: 0x3b,
    F2: 0x3c,
    F3: 0x3d,
    F4: 0x3e,
    F5: 0x3f,
    F6: 0x40,
    F7: 0x41,
    F8: 0x42,
    F9: 0x43,
    F10: 0x44,
    F11: 0x57,
    F12: 0x58,
  };

  const pressKey = useCallback(async (code: string, holdMs = 25) => {
    const scancode = scancodeMap[code];
    if (!emulatorRef.current || scancode === undefined) {
      return;
    }

    const sendScancode = (rawCode: number, released: boolean) => {
      if (rawCode > 0xff) {
        emulatorRef.current?.bus.send("keyboard-code", 0xe0);
        emulatorRef.current?.bus.send(
          "keyboard-code",
          released ? (rawCode & 0xff) | 0x80 : rawCode & 0xff,
        );
        return;
      }

      emulatorRef.current?.bus.send("keyboard-code", released ? rawCode | 0x80 : rawCode);
    };

    sendScancode(scancode, false);
    await new Promise((resolve) => window.setTimeout(resolve, holdMs));
    sendScancode(scancode, true);
  }, []);

  const pressCombo = useCallback(async (codes: readonly string[]) => {
    if (!emulatorRef.current) {
      return;
    }

    const sendScancode = (rawCode: number, released: boolean) => {
      if (rawCode > 0xff) {
        emulatorRef.current?.bus.send("keyboard-code", 0xe0);
        emulatorRef.current?.bus.send(
          "keyboard-code",
          released ? (rawCode & 0xff) | 0x80 : rawCode & 0xff,
        );
        return;
      }

      emulatorRef.current?.bus.send("keyboard-code", released ? rawCode | 0x80 : rawCode);
    };

    const resolved = codes
      .map((code) => scancodeMap[code])
      .filter((value): value is number => value !== undefined);

    for (const code of resolved) {
      sendScancode(code, false);
    }

    await new Promise((resolve) => window.setTimeout(resolve, 40));

    for (const code of [...resolved].reverse()) {
      sendScancode(code, true);
    }
  }, []);

  const charToCode = useCallback((char: string) => {
    if (char >= "a" && char <= "z") {
      return [`Key${char.toUpperCase()}`];
    }

    if (char >= "A" && char <= "Z") {
      return ["ShiftLeft", `Key${char}`];
    }

    if (char >= "0" && char <= "9") {
      return [`Digit${char}`];
    }

    const punctuationMap: Record<string, string[]> = {
      " ": ["Space"],
      "-": ["Minus"],
      "=": ["Equal"],
      "/": ["Slash"],
      ".": ["Period"],
      ",": ["Comma"],
    };

    return punctuationMap[char] ?? null;
  }, []);

  const sendText = useCallback(async (value: string, submit = false) => {
    for (const char of value) {
      const codes = charToCode(char);
      if (!codes) {
        continue;
      }

      if (codes.length === 1) {
        await pressKey(codes[0], 15);
      } else {
        await pressCombo(codes);
      }
      await new Promise((resolve) => window.setTimeout(resolve, 10));
    }

    if (submit) {
      await pressKey("Enter", 20);
    }
  }, [charToCode, pressCombo, pressKey]);

  const handleScreenClick = useCallback(() => {
    if (screenRef.current instanceof HTMLElement) {
      screenRef.current.focus();
      setVmFocused(true);
    }
  }, []);

  const handleScreenKeyDown = useCallback(
    (event: ReactKeyboardEvent<HTMLDivElement>) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        handleScreenClick();
      }
    },
    [handleScreenClick],
  );

  const handleScreenFocus = useCallback(() => {
    setVmFocused(true);
  }, []);

  const handleScreenBlur = useCallback(() => {
    setVmFocused(false);
  }, []);

  useEffect(() => {
    if (status !== "running") {
      return;
    }

    const timer = window.setTimeout(() => {
      void (async () => {
        handleScreenClick();
        await new Promise((resolve) => window.setTimeout(resolve, 120));
        await pressCombo(AUTO_SHELL_COMBO);
      })();
    }, 1200);

    return () => window.clearTimeout(timer);
  }, [handleScreenClick, pressCombo, status]);

  const submitShellCommand = useCallback(async () => {
    if (!shellCommand.trim()) {
      return;
    }

    handleScreenClick();
    await new Promise((resolve) => window.setTimeout(resolve, 40));
    await sendText(shellCommand.trim(), true);
  }, [handleScreenClick, sendText, shellCommand]);

  const missingAssets = assets.filter((asset) => asset.requiredForBoot && !asset.present);
  const canBoot = missingAssets.length === 0;
  const isRunning = status === "running";
  const statusLabel =
    status === "running"
      ? "Live"
      : status === "ready_to_boot"
        ? "Ready"
        : status === "booting"
          ? "Booting"
          : status === "missing_assets"
            ? "Missing assets"
            : status === "error"
              ? "Boot failed"
              : "Checking";

  return (
    <CRTFrame>
      <div className="mx-auto flex h-full w-full max-w-[1320px] min-h-0 flex-col gap-3 px-2 py-2 text-[#d4ffd4] lg:flex-row lg:items-stretch lg:justify-center">
        <section
          className="flex min-h-0 w-full max-w-[820px] flex-col overflow-hidden rounded-[24px] border border-[#4e6b53] bg-[rgba(1,9,3,0.82)] p-3 shadow-[0_0_32px_rgba(35,120,50,0.18)]"
        >
          <div className="mb-2 flex items-center justify-between gap-3 border-b border-[#29432d] pb-2">
            <div>
              <p className="font-mono text-xs uppercase tracking-[0.3em] text-[#7fb98a]">
                SimpleOS
              </p>
              <h1 className="font-mono text-lg text-[#ecffed]">Kernel Demo</h1>
            </div>

            <div className="flex flex-wrap items-center justify-end gap-2">
              <span className="rounded-full border border-[#29432d] px-3 py-1 font-mono text-[11px] text-[#89c193]">
                {statusLabel}
              </span>
              <button
                type="button"
                onClick={() => void bootEmulator()}
                disabled={!canBoot || status === "booting"}
                className="rounded-full border border-[#7fcf8c] bg-[#0f2a15] px-3 py-1 font-mono text-xs text-[#eaffec] transition enabled:hover:bg-[#17381e] disabled:cursor-not-allowed disabled:border-[#3d5742] disabled:text-[#6a816d]"
              >
                {status === "booting" ? "Booting..." : status === "running" ? "Reboot" : "Boot"}
              </button>
              <button
                type="button"
                onClick={restart}
                disabled={status !== "running"}
                className="rounded-full border border-[#4c7c56] px-3 py-1 font-mono text-xs text-[#d4ffd4] transition enabled:hover:border-[#8fd39a] enabled:hover:text-white disabled:cursor-not-allowed disabled:border-[#304233] disabled:text-[#617264]"
              >
                Restart
              </button>
            </div>
          </div>

          <div className="relative flex-1 overflow-hidden rounded-[18px] border border-[#243c28] bg-black">
            {(status === "checking" || status === "booting" || status === "missing_assets" || status === "error") && (
              <div className="absolute inset-0 z-10 flex flex-col items-center justify-center bg-[rgba(0,0,0,0.86)] px-6 text-center">
                <p className="mb-2 font-mono text-xl text-[#b9ffc4]">
                  {status === "checking" && "Checking demo assets"}
                  {status === "booting" && "Booting SimpleOS"}
                  {status === "missing_assets" && "Demo assets missing"}
                  {status === "error" && "Boot failed"}
                </p>
                <p className="max-w-[560px] font-mono text-sm leading-6 text-[#7fb98a]">
                  {status === "checking" &&
                    "The web demo now verifies its own prerequisites before trying to start the emulator."}
                  {status === "booting" &&
                    "Launching the x86 emulator and mounting the current SimpleOS ISO image."}
                  {status === "missing_assets" &&
                    "The browser shell is intentionally gated until BIOS images and the bootable ISO are in place."}
                  {status === "error" && (error ?? "The emulator could not be started with the current assets.")}
                </p>
              </div>
            )}

            <div className="flex h-full min-h-[340px] flex-col rounded-[18px] border border-[#2d4d34] bg-[linear-gradient(180deg,rgba(6,14,8,0.98),rgba(1,3,1,0.98))] p-2">
              <div className="mb-2 flex items-center justify-between gap-3 rounded-[12px] border border-[#203123] bg-[rgba(9,18,10,0.92)] px-3 py-2 font-mono text-[11px] text-[#8fbd96]">
                <div className="flex items-center gap-2">
                  <div className="flex gap-1.5">
                    {TUI_DOTS.map((color) => (
                      <span
                        key={color}
                        className="h-2.5 w-2.5 rounded-full border border-black/20"
                        style={{ backgroundColor: color }}
                      />
                    ))}
                  </div>
                  <span className="uppercase tracking-[0.2em] text-[#9de4a7]">system.log</span>
                </div>
                <span>{vmFocused ? "attached" : "view only"}</span>
              </div>

              <div
                ref={screenRef}
                role="button"
                tabIndex={status === "running" ? 0 : -1}
                aria-label="SimpleOS live VM display. Press Enter to focus keyboard input, or Escape to release it."
                onClick={handleScreenClick}
                onKeyDown={handleScreenKeyDown}
                onFocus={handleScreenFocus}
                onBlur={handleScreenBlur}
                className={`vm-screen flex-1 rounded-[14px] border border-[#38583d] bg-black outline-none ${
                  vmFocused ? "ring-2 ring-inset ring-[#7fcf8c]" : ""
                }`}
                style={{ cursor: status === "running" ? "text" : "default" }}
              />
            </div>
          </div>

          <div className="mt-2 flex flex-wrap gap-2 font-mono text-[11px] text-[#89c193]">
            <span className="rounded-full border border-[#29432d] px-3 py-1">
              {status !== "running" ? "Idle" : vmFocused ? "Display active" : "Click display to type"}
            </span>
            {isRunning && (
              <span className="rounded-full border border-[#29432d] px-3 py-1">
                Live output
              </span>
            )}
          </div>

          <section className="mt-2 space-y-2">
            <div className="min-h-[126px] rounded-[18px] border border-[#314c36] bg-[rgba(5,18,7,0.7)] p-3 font-mono text-[11px] leading-5 text-[#cfe9d2]">
              {isRunning ? (
                <>
                  <div className="rounded-[12px] border border-[#203123] bg-[rgba(9,18,10,0.92)] p-2">
                    <div className="mb-2 flex items-center justify-between gap-3 text-[11px] text-[#8fbd96]">
                      <div className="flex items-center gap-2">
                        <div className="flex gap-1.5">
                          {TUI_DOTS.map((color) => (
                            <span
                              key={`shell-${color}`}
                              className="h-2.5 w-2.5 rounded-full border border-black/20"
                              style={{ backgroundColor: color }}
                            />
                          ))}
                        </div>
                        <h2 className="uppercase tracking-[0.2em] text-[#9de4a7]">
                          shell.input
                        </h2>
                      </div>
                      <p className="text-[#6fa879]">shell auto-starts after boot</p>
                    </div>

                    <div className="flex gap-2">
                      <span className="flex items-center rounded-full border border-[#315137] bg-black px-3 text-[#7fcf8c]">
                        $
                      </span>
                      <input
                        value={shellCommand}
                        onChange={(event) => setShellCommand(event.target.value)}
                        className="flex-1 rounded-full border border-[#3f6f48] bg-black px-3 py-2 text-[#eaffec] outline-none placeholder:text-[#607764]"
                        placeholder="Type a shell command"
                        aria-label="Shell terminal command input"
                      />
                      <button
                        type="button"
                        onClick={() => void submitShellCommand()}
                        className="rounded-full border border-[#7fcf8c] bg-[#0f2a15] px-4 py-2 text-[#eaffec] transition hover:bg-[#17381e]"
                      >
                        Run
                      </button>
                    </div>

                    <div className="mt-2 flex flex-wrap gap-2">
                      {SHELL_COMMANDS.map((command) => (
                        <button
                          key={command}
                          type="button"
                          onClick={() => setShellCommand(command)}
                          className="rounded-full border border-[#2e5134] px-2 py-1 text-[#9fd0a7] transition hover:border-[#8fd39a] hover:text-white"
                        >
                          {command}
                        </button>
                      ))}
                    </div>
                  </div>
                </>
              ) : (
                <div className="flex h-full items-center justify-center text-center text-[#6fa879]">
                  <p>The shell input will appear here after boot.</p>
                </div>
              )}
            </div>
          </section>
        </section>

        <aside className="flex w-full max-w-[360px] min-h-0 flex-col gap-3 overflow-hidden pr-1 font-mono">
          <section className="shrink-0 rounded-[20px] border border-[#4e6b53] bg-[rgba(1,9,3,0.82)] p-3">
            <h2 className="mb-3 text-sm uppercase tracking-[0.2em] text-[#9de4a7]">
              {isRunning ? "Quick Help" : "Assets"}
            </h2>
            {isRunning ? (
              <div className="space-y-2 text-sm leading-6 text-[#d4ffd4]">
                <p className="rounded-xl border border-[#243c28] bg-[rgba(4,18,7,0.65)] px-3 py-2">
                  `system.log` is the live kernel display.
                </p>
                <p className="rounded-xl border border-[#243c28] bg-[rgba(4,18,7,0.65)] px-3 py-2">
                  `shell.input` sends commands into the running shell.
                </p>
              </div>
            ) : (
              <div className="space-y-2 text-sm">
                {assets.map((asset) => (
                  <div
                    key={asset.key}
                    className="flex items-center justify-between rounded-xl border border-[#243c28] bg-[rgba(4,18,7,0.65)] px-3 py-2"
                  >
                    <span className="text-[#d4ffd4]">{asset.label}</span>
                    <span className={asset.present ? "text-[#7cf08a]" : "text-[#ff938f]"}>
                      {asset.present ? "present" : "missing"}
                    </span>
                  </div>
                ))}
              </div>
            )}
          </section>

          <section className="shrink-0 rounded-[20px] border border-[#4e6b53] bg-[rgba(1,9,3,0.82)] p-3">
            <h2 className="mb-3 text-sm uppercase tracking-[0.2em] text-[#9de4a7]">
              {isRunning ? "Tips" : "Setup"}
            </h2>
            {isRunning ? (
              <div className="space-y-2 text-sm leading-6 text-[#d4ffd4]">
                <p>The shell appears a moment after boot.</p>
                <p>Click the display if you want direct keyboard input.</p>
                <p>`Restart` keeps the layout fixed and reboots the ISO.</p>
              </div>
            ) : (
              <>
                <ol className="space-y-2 text-sm leading-6 text-[#d4ffd4]">
                  {DEMO_COMMANDS.map((command, index) => (
                    <li key={command}>
                      <span className="mr-2 text-[#7fb98a]">{index + 1}.</span>
                      <code>{command}</code>
                    </li>
                  ))}
                </ol>
                {!canBoot && (
                  <p className="mt-3 rounded-xl border border-[#543432] bg-[rgba(35,10,10,0.5)] px-3 py-2 text-sm leading-6 text-[#ffc9c5]">
                    Missing boot assets:
                    {" "}
                    {missingAssets.map((asset) => asset.label).join(", ")}.
                  </p>
                )}
              </>
            )}
          </section>
        </aside>
      </div>
    </CRTFrame>
  );
}
