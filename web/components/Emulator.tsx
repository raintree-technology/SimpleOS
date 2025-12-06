"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import CRTFrame from "./CRTFrame";

declare global {
  interface Window {
    V86: new (options: V86Options) => V86Instance;
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
  run: () => void;
  keyboard_send_scancodes: (codes: number[]) => void;
  keyboard_set_status: (enabled: boolean) => void;
  bus: {
    send: (event: string, data: number) => void;
  };
}

export default function Emulator() {
  const screenRef = useRef<HTMLDivElement>(null);
  const emulatorRef = useRef<V86Instance | null>(null);
  const [status, setStatus] = useState<"loading" | "ready" | "error">("loading");
  const [error, setError] = useState<string | null>(null);

  const startEmulator = useCallback(async () => {
    if (!screenRef.current || !window.V86) return;

    // Clear any existing content - v86 will create its own canvas/div
    screenRef.current.innerHTML = "";

    try {
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
        setStatus("ready");
        console.log("Emulator ready");

        // Focus the screen container for keyboard input
        if (screenRef.current) {
          const canvas = screenRef.current.querySelector("canvas");
          if (canvas) {
            canvas.tabIndex = 0;
            canvas.focus();
            console.log("Focused canvas for keyboard input");
          }
        }
      });
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to start emulator");
      setStatus("error");
    }
  }, []);

  const restart = useCallback(() => {
    emulatorRef.current?.restart();
  }, []);

  // Focus the canvas when clicking on the screen area
  const handleScreenClick = useCallback(() => {
    if (screenRef.current) {
      const canvas = screenRef.current.querySelector("canvas");
      if (canvas) {
        canvas.focus();
        console.log("Clicked - focused canvas");
      }
    }
  }, []);

  useEffect(() => {
    const script = document.createElement("script");
    script.src = "/v86/libv86.js";
    script.async = true;
    script.onload = () => {
      console.log("v86 script loaded");
      startEmulator();
    };
    script.onerror = () => {
      setError("Failed to load v86 library");
      setStatus("error");
    };
    document.head.appendChild(script);

    return () => {
      emulatorRef.current?.stop();
      script.remove();
    };
  }, [startEmulator]);

  // Manually forward keyboard events to v86 as a fallback
  useEffect(() => {
    if (status !== "ready" || !emulatorRef.current) return;

    // PS/2 Set 1 scancodes for common keys
    const scancodeMap: Record<string, number> = {
      KeyA: 0x1E, KeyB: 0x30, KeyC: 0x2E, KeyD: 0x20, KeyE: 0x12, KeyF: 0x21,
      KeyG: 0x22, KeyH: 0x23, KeyI: 0x17, KeyJ: 0x24, KeyK: 0x25, KeyL: 0x26,
      KeyM: 0x32, KeyN: 0x31, KeyO: 0x18, KeyP: 0x19, KeyQ: 0x10, KeyR: 0x13,
      KeyS: 0x1F, KeyT: 0x14, KeyU: 0x16, KeyV: 0x2F, KeyW: 0x11, KeyX: 0x2D,
      KeyY: 0x15, KeyZ: 0x2C,
      Digit0: 0x0B, Digit1: 0x02, Digit2: 0x03, Digit3: 0x04, Digit4: 0x05,
      Digit5: 0x06, Digit6: 0x07, Digit7: 0x08, Digit8: 0x09, Digit9: 0x0A,
      Space: 0x39, Enter: 0x1C, Backspace: 0x0E, Tab: 0x0F, Escape: 0x01,
      Minus: 0x0C, Equal: 0x0D, BracketLeft: 0x1A, BracketRight: 0x1B,
      Backslash: 0x2B, Semicolon: 0x27, Quote: 0x28, Backquote: 0x29,
      Comma: 0x33, Period: 0x34, Slash: 0x35,
      ShiftLeft: 0x2A, ShiftRight: 0x36, ControlLeft: 0x1D, AltLeft: 0x38,
      ArrowUp: 0xE048, ArrowDown: 0xE050, ArrowLeft: 0xE04B, ArrowRight: 0xE04D,
      F1: 0x3B, F2: 0x3C, F3: 0x3D, F4: 0x3E, F5: 0x3F, F6: 0x40,
      F7: 0x41, F8: 0x42, F9: 0x43, F10: 0x44, F11: 0x57, F12: 0x58,
    };

    const handleKeyDown = (e: KeyboardEvent) => {
      // Don't handle if typing in an input/textarea
      const target = e.target as HTMLElement;
      if (target.tagName === "INPUT" || target.tagName === "TEXTAREA") return;

      const scancode = scancodeMap[e.code];
      if (scancode !== undefined) {
        e.preventDefault();
        if (scancode > 0xFF) {
          // Extended key (0xE0 prefix)
          emulatorRef.current?.bus.send("keyboard-code", 0xE0);
          emulatorRef.current?.bus.send("keyboard-code", scancode & 0xFF);
        } else {
          emulatorRef.current?.bus.send("keyboard-code", scancode);
        }
        console.log("Key down:", e.code, "scancode:", scancode.toString(16));
      }
    };

    const handleKeyUp = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      if (target.tagName === "INPUT" || target.tagName === "TEXTAREA") return;

      const scancode = scancodeMap[e.code];
      if (scancode !== undefined) {
        e.preventDefault();
        if (scancode > 0xFF) {
          emulatorRef.current?.bus.send("keyboard-code", 0xE0);
          emulatorRef.current?.bus.send("keyboard-code", (scancode & 0xFF) | 0x80);
        } else {
          emulatorRef.current?.bus.send("keyboard-code", scancode | 0x80);
        }
      }
    };

    console.log("Adding keyboard event listeners");
    window.addEventListener("keydown", handleKeyDown, { capture: true });
    window.addEventListener("keyup", handleKeyUp, { capture: true });

    return () => {
      window.removeEventListener("keydown", handleKeyDown, { capture: true });
      window.removeEventListener("keyup", handleKeyUp, { capture: true });
    };
  }, [status]);

  return (
    <CRTFrame>
      <div className="relative">
        {status === "loading" && (
          <div className="absolute inset-0 flex flex-col items-center justify-center text-green-400 z-10 bg-black"
               style={{ width: 800, height: 500 }}>
            <div className="text-xl mb-2 font-mono">Loading SimpleOS...</div>
            <div className="text-sm text-green-600 font-mono">Initializing x86 emulator</div>
            <div className="mt-4 flex gap-1">
              <div className="w-2 h-2 bg-green-500 rounded-full animate-pulse" style={{ animationDelay: '0ms' }} />
              <div className="w-2 h-2 bg-green-500 rounded-full animate-pulse" style={{ animationDelay: '150ms' }} />
              <div className="w-2 h-2 bg-green-500 rounded-full animate-pulse" style={{ animationDelay: '300ms' }} />
            </div>
          </div>
        )}

        {status === "error" && (
          <div className="absolute inset-0 flex flex-col items-center justify-center text-red-400 z-10 bg-black"
               style={{ width: 800, height: 500 }}>
            <div className="text-xl mb-2 font-mono">ERROR</div>
            <div className="text-sm text-red-600 font-mono">{error}</div>
          </div>
        )}

        {/* v86 screen container - v86 creates its own canvas/div */}
        <div
          ref={screenRef}
          onClick={handleScreenClick}
          style={{
            width: "800px",
            height: "500px",
            backgroundColor: "black",
            cursor: "text",
          }}
        />

        {status === "ready" && (
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation();
              restart();
            }}
            className="absolute top-2 right-2 px-2 py-1 bg-black/50 text-green-400 text-xs font-mono rounded
                       opacity-30 hover:opacity-100 transition-opacity border border-green-800 z-20"
          >
            RESTART
          </button>
        )}
      </div>
    </CRTFrame>
  );
}
