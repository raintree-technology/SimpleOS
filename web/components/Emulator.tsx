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
}

interface V86Instance {
  add_listener: (event: string, callback: () => void) => void;
  restart: () => void;
  stop: () => void;
  run: () => void;
}

export default function Emulator() {
  const screenRef = useRef<HTMLDivElement>(null);
  const emulatorRef = useRef<V86Instance | null>(null);
  const [status, setStatus] = useState<"loading" | "ready" | "error">(
    "loading",
  );
  const [error, setError] = useState<string | null>(null);
  const [focused, setFocused] = useState(false);

  const startEmulator = useCallback(async () => {
    if (!screenRef.current || !window.V86) return;

    // Ensure canvas exists in container
    const canvas = screenRef.current.querySelector("canvas");
    if (!canvas) {
      setError("Canvas element not found");
      setStatus("error");
      return;
    }

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
      });

      emulatorRef.current.add_listener("emulator-ready", () => {
        setStatus("ready");
        // Auto-focus the screen container for keyboard input
        const canvas = screenRef.current?.querySelector("canvas");
        if (canvas) {
          canvas.tabIndex = 0;
          canvas.focus();
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

  useEffect(() => {
    // Load v86 script
    const script = document.createElement("script");
    script.src = "/v86/libv86.js";
    script.async = true;
    script.onload = () => {
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

        <div
          ref={screenRef}
          className="flex items-center justify-center cursor-pointer"
          style={{
            width: "800px",
            height: "500px",
            overflow: "hidden"
          }}
          onClick={() => {
            const canvas = screenRef.current?.querySelector("canvas");
            if (canvas) {
              canvas.tabIndex = 0;
              canvas.focus();
              setFocused(true);
            }
          }}
        >
          <div style={{ whiteSpace: "pre", font: "14px monospace", lineHeight: "14px" }} />
          <canvas style={{ backgroundColor: "black", width: "100%", height: "100%", outline: "none" }} />
        </div>

        {status === "ready" && !focused && (
          <div
            className="absolute inset-0 flex items-center justify-center bg-black/50 cursor-pointer z-10"
            onClick={() => {
              const canvas = screenRef.current?.querySelector("canvas");
              if (canvas) {
                canvas.tabIndex = 0;
                canvas.focus();
                setFocused(true);
              }
            }}
          >
            <div className="text-green-400 font-mono text-lg animate-pulse">
              Click to start typing
            </div>
          </div>
        )}

        {status === "ready" && (
          <button
            type="button"
            onClick={restart}
            className="absolute top-2 right-2 px-2 py-1 bg-black/50 text-green-400 text-xs font-mono rounded
                       opacity-30 hover:opacity-100 transition-opacity border border-green-800"
          >
            RESTART
          </button>
        )}
      </div>
    </CRTFrame>
  );
}
