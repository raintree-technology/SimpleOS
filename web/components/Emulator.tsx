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

interface TerminalLine {
  id: string;
  text: string;
}

interface DisplayRow {
  id: string;
  source: string;
  event: string;
  status: string;
  value: string;
  hotkey: string;
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

const SHELL_COMMANDS = ["help", "ps", "ls", "ls /bin", "echo hello", "clear"];

const BOOT_STEPS = [
  "Probe runtime assets",
  "Load BIOS and VGA firmware",
  "Mount SimpleOS ISO",
  "Attach keyboard bridge",
];

const SOCIAL_LINKS = [
  {
    label: "X",
    href: "https://x.com/zacharyr0th",
    icon: (
      <svg viewBox="0 0 24 24" aria-hidden="true" className="h-4 w-4 fill-current">
        <path d="M18.901 1.154h3.68l-8.04 9.19L24 22.846h-7.406l-5.8-7.584-6.64 7.584H.47l8.6-9.83L0 1.154h7.594l5.243 6.932zM17.604 20.634h2.039L6.486 3.25H4.298z" />
      </svg>
    ),
  },
  {
    label: "LinkedIn",
    href: "https://www.linkedin.com/in/zacharyr0th",
    icon: (
      <svg viewBox="0 0 24 24" aria-hidden="true" className="h-4 w-4 fill-current">
        <path d="M4.983 3.5C4.983 4.881 3.87 6 2.497 6A2.5 2.5 0 010 3.5C0 2.119 1.113 1 2.497 1a2.5 2.5 0 012.486 2.5zM.5 8h4V23h-4zM8 8h3.833v2.047h.055C12.422 9.078 13.97 8 16.167 8 20.75 8 22 10.838 22 15.531V23h-4v-6.625c0-1.578-.027-3.609-2.2-3.609-2.203 0-2.54 1.719-2.54 3.492V23H8z" />
      </svg>
    ),
  },
  {
    label: "Website",
    href: "https://zacharyr0th.com",
    icon: (
      <svg viewBox="0 0 24 24" aria-hidden="true" className="h-4 w-4 fill-none stroke-current stroke-2">
        <circle cx="12" cy="12" r="9" />
        <path d="M3 12h18" />
        <path d="M12 3c2.8 3 4.2 6 4.2 9s-1.4 6-4.2 9c-2.8-3-4.2-6-4.2-9s1.4-6 4.2-9z" />
      </svg>
    ),
  },
] as const;

const PROJECT_LINKS = [
  {
    id: "about",
    label: "About",
  },
  {
    id: "why",
    label: "Why",
  },
  {
    id: "how",
    label: "How",
  },
] as const;

const PROJECT_COPY = {
  about: {
    title: "About this project",
    body:
      "SimpleOS is a kernel built from scratch as a learning project. It exists to explore how low-level systems work by booting a custom runtime, exposing a small shell, and surfacing basic kernel activity in the demo UI.",
  },
  why: {
    title: "Why I built it",
    body:
      "I built it to understand operating systems by writing one directly instead of only reading about them. It forced me to learn boot flow, memory, interrupts, process behavior, and low-level system tradeoffs.",
  },
  how: {
    title: "How I built it",
    body:
      "It is assembled as a small custom kernel, packaged into a bootable image, and run here inside a browser emulator. The UI wraps that runtime with boot controls, a minimal shell, and a cleaner display layer for kernel output.",
  },
} as const;

const DISPLAY_SOURCES = new Set(["Kernel Thread A", "Kernel Thread B", "Memory Check", "Syscall Bridge"]);
const DISPLAY_ROW_LIMIT = 8;

function toTitleCase(value: string) {
  return value
    .split(/[\s_-]+/)
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1).toLowerCase())
    .join(" ");
}

function parseDisplayLine(line: string, index: number): DisplayRow | null {
  const baseMatch = line.match(/^\[(.+?)\]\s*(.+?)(?:\s+\[(F\d+)\])?$/);
  const source = baseMatch?.[1];
  const message = baseMatch?.[2];
  const hotkey = baseMatch?.[3] ?? "AUTO";

  if (!source || !message || !DISPLAY_SOURCES.has(source)) {
    return null;
  }

  let event = "Log";
  let status = "Active";
  let value = message;

  const heartbeatMatch = message.match(/^Heartbeat\s+(\d+)$/i);
  const resultMatch = message.match(/^Result:\s+(.+)$/i);

  if (heartbeatMatch) {
    event = "Heartbeat";
    status = "Streaming";
    value = heartbeatMatch[1];
  } else if (/Waiting for requests/i.test(message)) {
    event = "Queue";
    status = "Idle";
    value = "Requests";
  } else if (/Dispatch loop online/i.test(message)) {
    event = "Bridge";
    status = "Online";
    value = "Dispatch";
  } else if (/Background monitor online/i.test(message)) {
    event = "Monitor";
    status = "Online";
    value = "Memory";
  } else if (/Starting stack validation/i.test(message)) {
    event = "Validation";
    status = "Running";
    value = "Stack";
  } else if (resultMatch) {
    event = "Validation";
    status = toTitleCase(resultMatch[1]);
    value = "Result";
  } else {
    const [firstWord, ...rest] = message.split(/\s+/);
    event = toTitleCase(firstWord ?? "Log");
    status = rest.length > 0 ? rest.join(" ") : "Live";
    value = source;
  }

  return {
    id: `${source}-${message}-${hotkey}-${index}`,
    source,
    event,
    status,
    value,
    hotkey,
  };
}

function parseDisplayRows(rawText: string): DisplayRow[] {
  return rawText
    .split("\n")
    .map((line) => line.replace(/\s+/g, " ").trim())
    .filter(Boolean)
    .slice(-32)
    .map((line, index) => parseDisplayLine(line, index))
    .filter((row): row is DisplayRow => row !== null)
    .slice(-DISPLAY_ROW_LIMIT);
}

function createTerminalLines(lines: string[]): TerminalLine[] {
  return lines.map((text, index) => ({
    id: `terminal-${Date.now()}-${index}-${text}`,
    text,
  }));
}

function runDemoCommand(command: string): string[] {
  if (command === "clear") {
    return [];
  }

  if (command === "help") {
    return [
      "Available commands:",
      "help      show available commands",
      "ps        show running demo tasks",
      "ls        list mounted paths",
      "ls /bin   list demo binaries",
      "echo ...  print a message",
      "clear     clear the terminal",
    ];
  }

  if (command === "ps") {
    return [
      "PID  NAME           STATE",
      "1    testproc1      runnable",
      "2    testproc2      runnable",
      "3    syscalltest    waiting",
      "4    memorytest     sleeping",
      "5    shell          interactive",
    ];
  }

  if (command === "ls") {
    return ["bin", "dev", "home", "proc", "tmp"];
  }

  if (command === "ls /bin") {
    return ["shell", "hello", "grep", "wc"];
  }

  if (command.startsWith("echo ")) {
    return [command.slice(5)];
  }

  return [`${command}: command not found. Type 'help' for available commands.`];
}

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
  const terminalOutputRef = useRef<HTMLDivElement>(null);
  const emulatorRef = useRef<V86Instance | null>(null);
  const scriptRef = useRef<HTMLScriptElement | null>(null);
  const displayRowsKeyRef = useRef("");

  const [status, setStatus] = useState<EmulatorStatus>("checking");
  const [error, setError] = useState<string | null>(null);
  const [shellCommand, setShellCommand] = useState("help");
  const [projectSection, setProjectSection] = useState<keyof typeof PROJECT_COPY>("about");
  const [vmFocused, setVmFocused] = useState(false);
  const [terminalLines, setTerminalLines] = useState<TerminalLine[]>([]);
  const [displayRows, setDisplayRows] = useState<DisplayRow[]>([]);
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
    setDisplayRows([]);
    displayRowsKeyRef.current = "";

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

    const isInteractiveTarget = (target: EventTarget | null) => {
      if (!(target instanceof HTMLElement)) {
        return false;
      }

      if (screenRef.current?.contains(target)) {
        return false;
      }

      return Boolean(
        target.closest("input, textarea, button, select, option, a, [contenteditable='true']"),
      );
    };

    const handleKeyDown = (event: KeyboardEvent) => {
      if (isInteractiveTarget(event.target)) {
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
      if (isInteractiveTarget(event.target)) {
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
      setTerminalLines([]);
      return;
    }

    setTerminalLines(
      createTerminalLines([
        "SimpleOS shell",
        "Telemetry is on the left. Type commands here.",
        "Try 'help' to see what's available.",
        "",
      ]),
    );
  }, [status]);

  useEffect(() => {
    const node = terminalOutputRef.current;
    if (!node || terminalLines.length === 0) {
      return;
    }

    node.scrollTop = node.scrollHeight;
  }, [terminalLines]);

  useEffect(() => {
    if (status !== "running") {
      setDisplayRows([]);
      displayRowsKeyRef.current = "";
      return;
    }

    const container = screenRef.current;
    if (!container) {
      return;
    }

    let frame = 0;
    let timeoutId: number | null = null;

    const updateRows = () => {
      frame = 0;
      const textLayer = Array.from(container.children).find(
        (node): node is HTMLDivElement => node instanceof HTMLDivElement,
      );

      if (!textLayer) {
        setDisplayRows([]);
        return;
      }

      const rawText = textLayer.innerText || textLayer.textContent || "";
      const nextRows = parseDisplayRows(rawText);
      const nextKey = nextRows.map((row) => `${row.source}|${row.event}|${row.status}|${row.value}`).join("::");
      if (nextKey === displayRowsKeyRef.current) {
        return;
      }

      displayRowsKeyRef.current = nextKey;
      setDisplayRows(nextRows);
    };

    const scheduleUpdate = () => {
      if (frame !== 0) {
        cancelAnimationFrame(frame);
      }
      if (timeoutId !== null) {
        clearTimeout(timeoutId);
      }
      timeoutId = window.setTimeout(() => {
        frame = requestAnimationFrame(updateRows);
      }, 90);
    };

    scheduleUpdate();

    const observer = new MutationObserver(scheduleUpdate);
    observer.observe(container, {
      childList: true,
      subtree: true,
      characterData: true,
    });

    return () => {
      observer.disconnect();
      if (frame !== 0) {
        cancelAnimationFrame(frame);
      }
      if (timeoutId !== null) {
        clearTimeout(timeoutId);
      }
    };
  }, [status]);

  const charToScancode = useCallback((char: string): { code: number; shift: boolean } | null => {
    const baseMap: Record<string, number> = {
      a: 0x1e, b: 0x30, c: 0x2e, d: 0x20, e: 0x12, f: 0x21, g: 0x22, h: 0x23,
      i: 0x17, j: 0x24, k: 0x25, l: 0x26, m: 0x32, n: 0x31, o: 0x18, p: 0x19,
      q: 0x10, r: 0x13, s: 0x1f, t: 0x14, u: 0x16, v: 0x2f, w: 0x11, x: 0x2d,
      y: 0x15, z: 0x2c,
      "0": 0x0b, "1": 0x02, "2": 0x03, "3": 0x04, "4": 0x05,
      "5": 0x06, "6": 0x07, "7": 0x08, "8": 0x09, "9": 0x0a,
      " ": 0x39, "-": 0x0c, "=": 0x0d, "[": 0x1a, "]": 0x1b, "\\": 0x2b,
      ";": 0x27, "\'": 0x28, "`": 0x29, ",": 0x33, ".": 0x34, "/": 0x35,
    };
    const shiftMap: Record<string, number> = {
      "!": 0x02, "@": 0x03, "#": 0x04, "$": 0x05, "%": 0x06, "^": 0x07,
      "&": 0x08, "*": 0x09, "(": 0x0a, ")": 0x0b, "_": 0x0c, "+": 0x0d,
      "{": 0x1a, "}": 0x1b, "|": 0x2b, ":": 0x27, "\"": 0x28, "~": 0x29,
      "<": 0x33, ">": 0x34, "?": 0x35,
    };
    const lower = char.toLowerCase();
    if (baseMap[lower] !== undefined) return { code: baseMap[lower], shift: char !== lower };
    if (shiftMap[char] !== undefined) return { code: shiftMap[char], shift: true };
    return null;
  }, []);

  const sendStringToVM = useCallback((text: string, pressEnter = true) => {
    const emu = emulatorRef.current;
    if (!emu) return Promise.resolve();
    return new Promise<void>((resolve) => {
      let delay = 0;
      for (const char of text) {
        const entry = charToScancode(char);
        if (!entry) continue;
        const { code, shift } = entry;
        setTimeout(() => {
          if (shift) emu.bus.send("keyboard-code", 0x2a);
          emu.bus.send("keyboard-code", code);
          emu.bus.send("keyboard-code", code | 0x80);
          if (shift) emu.bus.send("keyboard-code", 0xaa);
        }, delay);
        delay += 8;
      }
      if (pressEnter) {
        setTimeout(() => {
          emu.bus.send("keyboard-code", 0x1c);
          emu.bus.send("keyboard-code", 0x1c | 0x80);
          resolve();
        }, delay);
        return;
      }

      setTimeout(() => resolve(), delay + 16);
    });
  }, [charToScancode]);

  const submitShellCommand = useCallback(async () => {
    const command = shellCommand.trim();
    if (!command) {
      return;
    }

    const output = runDemoCommand(command);
    setTerminalLines((current) => {
      if (command === "clear") {
        return createTerminalLines([
          "SimpleOS demo shell",
          "Telemetry stays on the left. This panel is an interactive terminal.",
          "Type 'help' for commands.",
          "",
        ]);
      }

      return [
        ...current,
        ...createTerminalLines([`$ ${command}`, ...output, ""]),
      ];
    });

    void sendStringToVM(command);
    setShellCommand("");
  }, [shellCommand, sendStringToVM]);

  const handleShellInputKeyDown = useCallback(
    (event: ReactKeyboardEvent<HTMLInputElement>) => {
      if (event.key === "Enter") {
        event.preventDefault();
        void submitShellCommand();
      }
    },
    [submitShellCommand],
  );

  const missingAssets = assets.filter((asset) => asset.requiredForBoot && !asset.present);
  const canBoot = missingAssets.length === 0;
  const isRunning = status === "running";
  const showTroubleshooting = status === "missing_assets" || status === "error";
  const showDisplayTable = isRunning && displayRows.length > 0;
  const displayPhaseLabel = showDisplayTable ? "Kernel telemetry" : isRunning ? "Syncing display" : "Awaiting boot";
  const activeProjectCopy = PROJECT_COPY[projectSection];
  const terminalLocked = !isRunning;
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
  const statusHeadline =
    status === "running"
      ? "Kernel session online"
      : status === "ready_to_boot"
        ? "Ready to boot"
        : status === "booting"
          ? "Starting emulator"
          : status === "missing_assets"
            ? "Boot assets missing"
            : status === "error"
              ? "Boot failed"
              : "Inspecting runtime";
  const statusCopy =
    status === "running"
      ? "System online. Click the display to capture keyboard input."
      : status === "ready_to_boot"
        ? "Ready to boot."
        : status === "booting"
          ? "Initializing runtime."
          : status === "missing_assets"
            ? "Some boot assets are missing. See setup steps below."
            : status === "error"
              ? (error ?? "Boot failed. Check the asset panel and try again.")
              : "Scanning for boot assets\u2026";
  const primaryActionLabel =
    status === "booting" ? "Booting..." : status === "running" ? "Reboot Demo" : "Boot Demo";
  const statusToneClass =
    status === "running"
      ? "border-emerald-400/30 bg-emerald-400/10 text-emerald-100"
      : status === "error" || status === "missing_assets"
        ? "border-rose-300/30 bg-rose-300/10 text-rose-100"
        : "border-cyan-300/25 bg-cyan-300/10 text-cyan-50";
  const completedBootSteps =
    status === "running"
      ? BOOT_STEPS.length
      : status === "ready_to_boot"
        ? 1
        : status === "booting"
          ? 3
          : 0;

  return (
    <CRTFrame>
      <div className="mx-auto flex min-h-full w-full max-w-[1380px] flex-col px-4 py-4 text-[#eef6ff] md:px-8 md:py-8 lg:h-full lg:min-h-0">
        <div className="flex flex-col gap-6 overflow-visible lg:min-h-0 lg:flex-1 lg:overflow-hidden xl:flex-row xl:items-stretch xl:justify-center">
        <section className="flex w-full max-w-[1040px] flex-1 flex-col gap-6 overflow-visible lg:min-h-0 lg:overflow-hidden">
          <section className="shrink-0 px-1 py-1 md:px-2">
            <div className="flex flex-col gap-6 lg:flex-row lg:items-start lg:justify-between">
              <div className="min-w-0">
                <div className="flex flex-wrap items-center gap-2 text-[#b8ccff]">
                  <span className="ui-brand px-1 py-1 text-white/90">
                    SimpleOS
                  </span>
                </div>
                <p className="ui-body mt-4 min-h-12 max-w-[40ch] text-[#d6e3ff]/74">
                  {statusCopy}
                </p>
              </div>

              <div className="flex shrink-0 flex-wrap items-center gap-3 pt-1">
                <button
                  type="button"
                  onClick={() => void bootEmulator()}
                  disabled={!canBoot || status === "booting"}
                  className="ui-micro min-w-[9.75rem] rounded-full border border-cyan-300/20 bg-[linear-gradient(180deg,rgba(128,233,255,0.12),rgba(114,164,255,0.07))] px-6 py-3 tracking-[0.16em] text-white transition enabled:hover:border-cyan-200/34 enabled:hover:bg-[linear-gradient(180deg,rgba(128,233,255,0.18),rgba(114,164,255,0.1))] disabled:cursor-not-allowed disabled:border-white/8 disabled:bg-white/[0.02] disabled:text-white/35"
                >
                  {primaryActionLabel}
                </button>
                <button
                  type="button"
                  onClick={restart}
                  disabled={status !== "running"}
                  className="ui-micro rounded-full border border-white/8 bg-white/[0.02] px-5 py-2.5 tracking-[0.16em] text-[#edf4ff] transition enabled:hover:bg-white/[0.05] enabled:hover:text-white disabled:cursor-not-allowed disabled:text-white/35"
                >
                  Hard Reset
                </button>
              </div>
            </div>
          </section>

          <div className="grid gap-6 overflow-visible lg:min-h-0 lg:flex-1 lg:overflow-hidden lg:grid-cols-[1.2fr_0.88fr]">
            <section className="relative flex min-h-0 flex-col rounded-[24px] border border-white/10 bg-[linear-gradient(180deg,rgba(8,10,20,0.34),rgba(7,8,16,0.16))] p-5 text-[#edf5ff] backdrop-blur-md md:p-6">
              {(status === "checking" || status === "booting" || status === "missing_assets" || status === "error") && (
                <div className="absolute inset-5 z-10 flex flex-col justify-between rounded-[20px] border border-white/10 bg-[linear-gradient(180deg,rgba(9,12,25,0.42),rgba(8,10,20,0.6))] p-5 backdrop-blur-md md:inset-6 md:p-6">
                  <div>
                    <div className="mb-4 flex items-center justify-between gap-3">
                      <p className="ui-micro text-[#bfd5ff]">
                        Startup
                      </p>
                      <span className={`ui-micro rounded-full border px-3 py-1 ${statusToneClass}`}>
                        {statusLabel}
                      </span>
                    </div>
                    <p className="text-[1.6rem] font-medium leading-tight tracking-[-0.02em] text-white">
                      {statusHeadline}
                    </p>
                    <p className="ui-body mt-4 max-w-[42ch] text-[#d4e3ff]/82">
                      {statusCopy}
                    </p>
                  </div>

                    <div className="grid gap-3 pt-6">
                      {BOOT_STEPS.map((step, index) => {
                      const complete = index < completedBootSteps;

                      return (
                        <div
                          key={step}
                          className="flex items-center gap-3 rounded-[16px] border border-white/8 bg-white/[0.025] px-4 py-3.5"
                        >
                          <span
                            className={`ui-micro flex h-7 w-7 items-center justify-center rounded-full border ${
                              complete
                                ? "border-emerald-300/40 bg-emerald-300/12 text-emerald-100"
                                : "border-white/12 bg-white/[0.025] text-white/55"
                            }`}
                          >
                            {index + 1}
                          </span>
                          <div>
                            <p className="ui-micro tracking-[0.14em] text-white/45">
                              Step {index + 1} of {BOOT_STEPS.length}
                            </p>
                            <p className="mt-1 text-[15px] font-medium leading-6 text-white">{step}</p>
                          </div>
                        </div>
                      );
                    })}
                  </div>
                </div>
              )}

              <div className="mb-5 flex items-start justify-between gap-4 text-[#dce8ff]">
                <div>
                  <p className="ui-micro text-white/45">Surface</p>
                  <h2 className="ui-panel-title mt-3 text-white">Logs</h2>
                </div>
                <span className="ui-micro rounded-full border border-white/10 bg-white/[0.025] px-3 py-1 text-white/45">
                  {vmFocused ? "Captured" : "Standby"}
                </span>
              </div>

              <div className="relative min-h-0 flex-1">
                <div
                  ref={screenRef}
                  role="button"
                  tabIndex={status === "running" ? 0 : -1}
                  aria-label="SimpleOS live VM display. Press Enter to focus keyboard input, or Escape to release it."
                  onClick={handleScreenClick}
                  onKeyDown={handleScreenKeyDown}
                  onFocus={handleScreenFocus}
                  onBlur={handleScreenBlur}
                  className={`vm-screen ${showDisplayTable ? "table-mode" : ""} h-full min-h-[240px] rounded-[20px] border border-white/10 bg-[linear-gradient(180deg,rgba(5,7,16,0.7),rgba(8,11,19,0.54))] shadow-[inset_0_1px_0_rgba(255,255,255,0.04)] outline-none sm:min-h-[320px]`}
                  style={{ cursor: status === "running" ? "text" : "default" }}
                />

                {isRunning && (
                  <div className="pointer-events-none absolute inset-0 rounded-[20px] bg-[rgba(7,10,18,0.92)]">
                    <div className="flex h-full flex-col overflow-hidden">
                      <div className="flex items-center justify-between gap-3 px-5 py-4">
                        <div className="ui-micro tracking-[0.12em] text-white/56">
                          Live telemetry
                        </div>
                        <div className="ui-micro tracking-[0.12em] text-white/40">
                          {displayPhaseLabel}
                        </div>
                      </div>

                      <div className="ui-micro grid grid-cols-[1.15fr_0.85fr_1.1fr_0.55fr] gap-2 border-b border-white/6 px-3 pb-3 pt-2 text-[9px] tracking-[0.12em] text-white/26 sm:grid-cols-[1.35fr_0.95fr_1.35fr_0.65fr] sm:gap-3 sm:px-5 sm:text-[10px]">
                        <span>Source</span>
                        <span>Event</span>
                        <span>Status</span>
                        <span className="text-right">Value</span>
                      </div>

                      <div className="min-h-0 flex-1 overflow-hidden">
                        {showDisplayTable ? (
                          <div className="grid auto-rows-fr">
                            {displayRows.map((row) => (
                              <div
                                key={row.id}
                                className="grid min-h-[48px] grid-cols-[1.15fr_0.85fr_1.1fr_0.55fr] gap-2 border-b border-white/[0.04] px-3 py-3 font-mono text-[11px] leading-4 text-[#eef5ff] last:border-b-0 sm:min-h-[52px] sm:grid-cols-[1.35fr_0.95fr_1.35fr_0.65fr] sm:gap-3 sm:px-5 sm:text-[15px] sm:leading-6"
                              >
                                <div className="min-w-0">
                                  <p className="truncate">{row.source}</p>
                                  <p className="ui-micro mt-1 tracking-[0.1em] text-white/28">
                                    {row.hotkey}
                                  </p>
                                </div>
                                <p className="truncate text-white/78">{row.event}</p>
                                <p className="truncate text-white/62">{row.status}</p>
                                <p className="truncate text-right text-white/88">{row.value}</p>
                              </div>
                            ))}
                          </div>
                        ) : (
                          <div className="flex h-full flex-col items-center justify-center gap-3 px-6 text-center">
                            <div className="h-1.5 w-1.5 rounded-full bg-white/45" />
                            <p className="ui-micro tracking-[0.12em] text-white/72">Waiting for kernel telemetry</p>
                            <p className="max-w-[34ch] text-[15px] leading-7 text-white/44">
                              Table populates once the kernel starts emitting events.
                            </p>
                          </div>
                        )}
                      </div>

                      {showDisplayTable && (
                        <div className="ui-micro grid grid-cols-2 gap-2 border-t border-white/6 px-3 py-3 text-[9px] tracking-[0.12em] text-white/28 sm:grid-cols-4 sm:px-5 sm:text-[10px]">
                          <span>{displayRows.length} rows live</span>
                          <span>{displayRows.filter((row) => row.event === "Heartbeat").length} heartbeats</span>
                          <span>{displayRows.filter((row) => row.status === "Online").length} online</span>
                          <span className="text-right">{vmFocused ? "captured" : "standby"}</span>
                        </div>
                      )}
                    </div>
                  </div>
                )}
              </div>

              <div className="mt-4 flex flex-wrap items-center justify-between gap-3 text-[#dce8ff]">
                <span className="ui-micro rounded-full border border-white/10 bg-white/[0.025] px-3 py-1 text-white/70">
                  {status !== "running" ? "Awaiting boot" : vmFocused ? "Keyboard captured" : "Click display to capture keyboard"}
                </span>
                <span className="ui-micro text-white/40">Press Esc to release keyboard</span>
              </div>
            </section>

            <section className="flex min-h-0 flex-col overflow-hidden rounded-[24px] border border-white/10 bg-[linear-gradient(180deg,rgba(8,10,20,0.34),rgba(7,8,16,0.16))] p-5 text-[#edf5ff] backdrop-blur-md md:p-6">
              <div className="mb-5 flex items-start justify-between gap-4 text-[#d4e2ff]">
                <div>
                  <p className="ui-micro text-white/45">Input</p>
                  <h2 className="ui-panel-title mt-3 text-white">Terminal</h2>
                </div>
                <p className="ui-micro rounded-full border border-white/10 bg-white/[0.025] px-3 py-1 text-white/45">
                  {terminalLocked ? "Locked" : "Interactive"}
                </p>
              </div>

              {isRunning ? (
                <div className="flex min-h-0 flex-1 flex-col">
                  <div
                    ref={terminalOutputRef}
                    className="ui-scrollbar mb-4 min-h-0 flex-1 overflow-auto rounded-[20px] border border-white/10 bg-[linear-gradient(180deg,rgba(5,7,16,0.7),rgba(8,11,19,0.54))] px-5 py-5 font-mono text-[15px] leading-7 text-white/80 shadow-[inset_0_1px_0_rgba(255,255,255,0.04)]"
                  >
                    {terminalLines.length > 0 ? (
                      <div className="space-y-2">
                        {terminalLines.map((line) => (
                          <p key={line.id} className="whitespace-pre-wrap break-words">
                            {line.text}
                          </p>
                        ))}
                      </div>
                    ) : (
                      <p className="text-white/45">Type a command below.</p>
                    )}
                  </div>

                  <div className="flex flex-col gap-3 sm:flex-row">
                    <div className="flex items-center gap-3 sm:flex-1">
                      <span className="flex h-11 items-center rounded-full border border-white/10 bg-white/[0.04] px-4 font-mono text-[14px] text-white/90">
                        $
                      </span>
                      <input
                        value={shellCommand}
                        onChange={(event) => setShellCommand(event.target.value)}
                        onKeyDown={handleShellInputKeyDown}
                        className="h-11 flex-1 rounded-full border border-white/10 bg-[rgba(10,14,28,0.54)] px-5 py-3 font-mono text-[15px] text-white outline-none placeholder:text-white/45"
                        placeholder="Type help, ps, ls..."
                        aria-label="Shell terminal command input"
                      />
                    </div>
                    <button
                      type="button"
                      onClick={() => void submitShellCommand()}
                      className="ui-micro h-11 rounded-full border border-cyan-300/25 bg-cyan-300/8 px-5 tracking-[0.16em] text-white transition hover:bg-cyan-300/14"
                    >
                      Run
                    </button>
                  </div>

                  <div className="mt-4 flex flex-wrap gap-2.5">
                    {SHELL_COMMANDS.map((command) => (
                      <button
                        key={command}
                        type="button"
                        onClick={() => setShellCommand(command)}
                        className="ui-micro rounded-full border border-white/10 bg-white/[0.035] px-3 py-1.5 text-[#dce8ff] transition hover:bg-white/10 hover:text-white"
                      >
                        {command}
                      </button>
                    ))}
                  </div>
                </div>
              ) : (
                <div className="flex min-h-0 flex-1 flex-col rounded-[20px] border border-white/10 bg-[linear-gradient(180deg,rgba(5,7,16,0.62),rgba(8,11,19,0.44))] p-5 shadow-[inset_0_1px_0_rgba(255,255,255,0.04)] md:p-6">
                  <div className="flex flex-1 flex-col gap-5">
                    <div className="min-h-[10.5rem]">
                      <p className="ui-micro text-white/45">
                        {activeProjectCopy.title}
                      </p>
                      <p className="ui-body mt-4 text-white/74">
                        {activeProjectCopy.body}
                      </p>
                    </div>

                    <div className="grid grid-cols-3 gap-2">
                      {PROJECT_LINKS.map((link) => (
                        <button
                          key={link.id}
                          type="button"
                          onClick={() => setProjectSection(link.id)}
                          className={`ui-micro min-w-0 rounded-full border px-3 py-1.5 text-center transition ${
                            projectSection === link.id
                              ? "border-cyan-200/28 bg-cyan-200/[0.08] text-white"
                              : "border-white/10 bg-white/[0.03] text-white/72 hover:bg-white/[0.08] hover:text-white"
                          }`}
                        >
                          <span className="block truncate">
                            {link.label}
                          </span>
                        </button>
                      ))}
                    </div>

                    <div className="mt-auto rounded-[16px] border border-white/10 bg-white/[0.025] p-4">
                      <p className="ui-micro text-white/45">
                        Unlocked after boot
                      </p>
                      <div className="mt-4 space-y-2 text-[15px] leading-7 text-white/72">
                        <p>Shell access</p>
                        <p>Keyboard capture</p>
                        <p>Live display output</p>
                      </div>
                    </div>
                  </div>
                </div>
              )}
            </section>
          </div>

          <footer className="mt-auto w-full px-1 pt-2">
            <div className="grid w-full items-end gap-4 pt-6 lg:grid-cols-[1.2fr_0.88fr]">
              <a
                href="https://zacharyr0th.com"
                target="_blank"
                rel="noreferrer"
                className="ui-micro text-[#d6e3ff]/68 transition hover:text-white"
              >
                Built by zacharyr0th
              </a>

              <div className="flex items-center justify-center gap-3 lg:justify-end">
                {SOCIAL_LINKS.map((link) => (
                  <a
                    key={link.label}
                    href={link.href}
                    target="_blank"
                    rel="noreferrer"
                    aria-label={link.label}
                    className="flex h-9 w-9 items-center justify-center rounded-full border border-white/10 bg-white/[0.03] text-[#edf5ff] transition hover:border-cyan-200/30 hover:bg-cyan-200/10 hover:text-white"
                  >
                    {link.icon}
                  </a>
                ))}
              </div>
            </div>
          </footer>
        </section>

          {showTroubleshooting && (
            <aside className="flex w-full max-w-[340px] flex-col gap-3 overflow-visible px-1 xl:min-h-0 xl:flex-[0_0_340px] xl:overflow-hidden">
            <section className="shrink-0 rounded-[22px] border border-white/10 bg-[linear-gradient(180deg,rgba(8,10,20,0.32),rgba(8,9,18,0.16))] p-4 backdrop-blur-md">
              <h2 className="mb-2 font-mono text-[10px] uppercase tracking-[0.2em] text-white/60">
                Assets
              </h2>
              <div className="space-y-2 text-[14px] leading-6">
                {assets.map((asset) => (
                  <div
                    key={asset.key}
                    className="flex items-center justify-between rounded-[16px] border border-white/8 bg-white/[0.03] px-3 py-2"
                  >
                    <span className="text-[#edf4ff]">{asset.label}</span>
                    <span className={asset.present ? "text-[#bffad3]" : "text-[#ffd1dc]"}>
                      {asset.present ? "ready" : "missing"}
                    </span>
                  </div>
                ))}
              </div>
            </section>

            <section className="shrink-0 rounded-[22px] border border-white/10 bg-[linear-gradient(180deg,rgba(8,10,20,0.32),rgba(8,9,18,0.16))] p-4 backdrop-blur-md">
              <h2 className="mb-2 font-mono text-[10px] uppercase tracking-[0.2em] text-white/60">
                Local setup
              </h2>
              <ol className="space-y-2 font-mono text-[13px] leading-6 text-[#edf4ff]">
                {DEMO_COMMANDS.map((command, index) => (
                  <li key={command}>
                    <span className="mr-2 text-white/60">{index + 1}.</span>
                    <code>{command}</code>
                  </li>
                ))}
              </ol>
              {!canBoot && (
                <p className="mt-3 rounded-[14px] bg-[rgba(255,170,196,0.12)] px-3 py-2 text-sm leading-6 text-[#ffe3ec]">
                  Missing: {missingAssets.map((asset) => asset.label).join(", ")}.
                  {" "}Run the setup commands above to build them.
                </p>
              )}
            </section>
            </aside>
          )}
        </div>

      </div>
    </CRTFrame>
  );
}
