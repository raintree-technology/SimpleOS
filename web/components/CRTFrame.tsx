"use client";

import { type ReactNode, useEffect, useRef, useState } from "react";

interface CRTFrameProps {
  children: ReactNode;
}

// ASCII characters for the swirl effect (from dense to sparse)
const ASCII_CHARS = "█▓▒░@%#*+=-:. ";
const TOP_VENT_KEYS = Array.from({ length: 16 }, (_, index) => `top-vent-${index}`);
const SIDE_VENT_KEYS = Array.from({ length: 10 }, (_, index) => `side-vent-${index}`);

export default function CRTFrame({ children }: CRTFrameProps) {
  const [powerOn, setPowerOn] = useState(false);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const animationRef = useRef<number>(0);

  useEffect(() => {
    const timer = setTimeout(() => setPowerOn(true), 300);
    return () => clearTimeout(timer);
  }, []);

  // Pastel ASCII vortex background animation
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext("2d", { alpha: false });
    if (!ctx) return;

    // Grid settings - denser for more coverage
    const charWidth = 16;
    const charHeight = 20;
    let cols = 0;
    let rows = 0;
    let cx = 0;
    let cy = 0;
    let maxDist = 0;

    const resize = () => {
      canvas.width = window.innerWidth;
      canvas.height = window.innerHeight;
      cols = Math.ceil(canvas.width / charWidth) + 4;
      rows = Math.ceil(canvas.height / charHeight) + 4;
      cx = canvas.width / 2;
      cy = canvas.height / 2;
      maxDist = Math.sqrt(cx * cx + cy * cy);
    };
    resize();
    window.addEventListener("resize", resize);

    let time = 0;
    let lastFrame = 0;
    const targetFPS = 30;
    const frameInterval = 1000 / targetFPS;

    // Smooth color transition between pastels
    let currentHue = 280; // Start with lavender
    let targetHue = 320; // Pink
    let hueTransitionProgress = 0;

    const charsLen = ASCII_CHARS.length;

    // Spiral arms
    const NUM_ARMS = 3;

    const animate = (timestamp: number) => {
      const delta = timestamp - lastFrame;
      if (delta < frameInterval) {
        animationRef.current = requestAnimationFrame(animate);
        return;
      }
      lastFrame = timestamp;
      time += 0.008;

      // Very smooth hue transitions through pastel range
      hueTransitionProgress += 0.001;
      if (hueTransitionProgress >= 1) {
        currentHue = targetHue;
        // Cycle through nice pastel hues: pinks, lavenders, soft blues, mints, peaches
        const pastelHues = [320, 280, 220, 180, 160, 30, 350];
        targetHue = pastelHues[Math.floor(Math.random() * pastelHues.length)];
        hueTransitionProgress = 0;
      }
      // Smoothstep interpolation
      const t = hueTransitionProgress * hueTransitionProgress * (3 - 2 * hueTransitionProgress);
      const baseHue = currentHue + (targetHue - currentHue) * t;

      // Clear
      ctx.fillStyle = "#0a0a12";
      ctx.fillRect(0, 0, canvas.width, canvas.height);

      // Draw vortex
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.font = `bold ${charHeight - 4}px monospace`;

      for (let row = 0; row < rows; row++) {
        for (let col = 0; col < cols; col++) {
          const gridX = col * charWidth - charWidth * 2;
          const gridY = row * charHeight - charHeight * 2;

          const dx = gridX - cx;
          const dy = gridY - cy;
          const dist = Math.sqrt(dx * dx + dy * dy);
          const angle = Math.atan2(dy, dx);

          // Normalized distance (0 at center, 1 at edge)
          const normalizedDist = Math.min(dist / maxDist, 1);

          // Global slow rotation of entire vortex
          const globalRotation = time * 0.15;

          // Vortex rotation - inner spins faster, outer slower
          const rotationSpeed = (1 - normalizedDist) ** 2 * 0.5 + 0.08;
          const vortexAngle = angle + globalRotation + time * rotationSpeed;

          // Spiral arm pattern - wide, soft arms
          const spiralTightness = 0.012;
          const armAngle = vortexAngle * NUM_ARMS + dist * spiralTightness - time * 0.8;

          // Soft arm falloff using smoothed cosine
          const rawArm = (Math.cos(armAngle) + 1) * 0.5;
          const armIntensity = rawArm ** 0.8; // Softer falloff = wider arms

          // Gentle radial pulse
          const pulse = Math.sin(dist * 0.015 - time * 1.2) * 0.15 + 0.85;

          // Distance fade - keep more visible further out
          const fade = (1 - normalizedDist * 0.5) ** 0.8;

          // Combined intensity
          const intensity = armIntensity * pulse * fade;

          if (intensity < 0.05) continue;

          // Character selection
          const charIndex = Math.min(Math.floor(intensity * charsLen), charsLen - 1);
          const char = ASCII_CHARS[charIndex];

          // Pastel colors - high lightness, moderate saturation
          const hueVariation = Math.sin(armAngle * 0.5) * 20;
          const hue = (baseHue + hueVariation + 360) % 360;
          const saturation = 35 + intensity * 25; // 35-60%
          const lightness = 25 + intensity * 35; // 25-60% - nice pastels
          const alpha = 0.6 + intensity * 0.4;

          ctx.fillStyle = `hsla(${hue}, ${saturation}%, ${lightness}%, ${alpha})`;
          ctx.fillText(char, gridX, gridY);
        }
      }

      // Soft center glow
      const gradient = ctx.createRadialGradient(cx, cy, 0, cx, cy, 200);
      gradient.addColorStop(0, `hsla(${baseHue}, 40%, 40%, 0.25)`);
      gradient.addColorStop(0.4, `hsla(${baseHue}, 35%, 30%, 0.1)`);
      gradient.addColorStop(1, "transparent");
      ctx.fillStyle = gradient;
      ctx.fillRect(cx - 200, cy - 200, 400, 400);

      animationRef.current = requestAnimationFrame(animate);
    };

    animationRef.current = requestAnimationFrame(animate);

    return () => {
      window.removeEventListener("resize", resize);
      cancelAnimationFrame(animationRef.current);
    };
  }, []);

  return (
    <div className="crt-room">
      {/* Animated ASCII background */}
      <canvas ref={canvasRef} className="ascii-bg" />

      {/* Ambient desk glow from monitor */}
      <div className="desk-glow" />

      {/* The monitor unit */}
      <div className={`monitor ${powerOn ? "powered" : ""}`}>
        {/* Top ventilation slots */}
        <div className="vent-top">
          {TOP_VENT_KEYS.map((key) => (
            <div key={key} className="vent-slot" />
          ))}
        </div>

        {/* Main monitor face */}
        <div className="monitor-face">
          {/* Recessed screen area */}
          <div className="screen-bezel">
            {/* The actual CRT glass */}
            <div className="crt-glass">
              <div className="screen-curve" />
              <div className="scanlines" />
              <div className="rgb-pixels" />
              <div className="phosphor-glow" />
              <div className="screen-glare" />
              <div className="flicker" />
              <div className="chromatic-left" />
              <div className="chromatic-right" />

              <div className={`screen-content ${powerOn ? "on" : ""}`}>
                {children}
              </div>
            </div>
          </div>

          {/* Monitor badge/logo */}
          <div className="monitor-badge">
            <span className="badge-text">SimpleOS</span>
            <span className="badge-model">Model M1984</span>
          </div>
        </div>

        {/* Bottom section with controls */}
        <div className="monitor-base">
          <div className="floppy-drive">
            <div className="floppy-slot" />
            <div className="floppy-light" />
          </div>

          <div className="controls">
            <div className="control-group">
              <div className="brightness-knob">
                <div className="knob-indicator" />
              </div>
              <span className="control-label">BRIGHT</span>
            </div>
            <div className="control-group">
              <div className="contrast-knob">
                <div className="knob-indicator" />
              </div>
              <span className="control-label">CONTRAST</span>
            </div>
          </div>

          <div className="power-section">
            <div className={`power-led ${powerOn ? "on" : ""}`} />
            <span className="power-label">POWER</span>
          </div>
        </div>

        {/* Side vents */}
        <div className="side-vent left">
          {SIDE_VENT_KEYS.map((key) => (
            <div key={`${key}-left`} className="vent-horizontal" />
          ))}
        </div>
        <div className="side-vent right">
          {SIDE_VENT_KEYS.map((key) => (
            <div key={`${key}-right`} className="vent-horizontal" />
          ))}
        </div>
      </div>

      <style jsx>{`
        .crt-room {
          height: 100dvh;
          min-height: 100dvh;
          display: flex;
          flex-direction: column;
          align-items: center;
          justify-content: center;
          background: #0a0a12;
          padding: 8px;
          position: relative;
          overflow: hidden;
        }

        .ascii-bg {
          position: absolute;
          top: 0;
          left: 0;
          width: 100%;
          height: 100%;
          z-index: 0;
        }

        .desk-glow {
          position: absolute;
          bottom: 0;
          left: 50%;
          transform: translateX(-50%);
          width: 900px;
          height: 350px;
          background: radial-gradient(ellipse at center top,
            rgba(200, 180, 220, 0.15) 0%,
            rgba(180, 200, 220, 0.08) 40%,
            transparent 70%
          );
          pointer-events: none;
          z-index: 1;
        }

        .monitor {
          position: relative;
          z-index: 2;
          display: flex;
          flex-direction: column;
          background: linear-gradient(180deg,
            #e8e4ef 0%,
            #ddd8e8 5%,
            #d0cbe0 50%,
            #c4bfd8 95%,
            #b8b3d0 100%
          );
          border-radius: 28px 28px 12px 12px;
          padding: 24px 30px 18px 30px;
          box-shadow:
            0 40px 100px rgba(100, 80, 150, 0.3),
            0 15px 40px rgba(80, 60, 120, 0.2),
            inset 0 2px 0 rgba(255, 255, 255, 0.6),
            inset 0 -2px 0 rgba(100, 80, 140, 0.1),
            inset 4px 0 8px rgba(100, 80, 140, 0.05),
            inset -4px 0 8px rgba(100, 80, 140, 0.05);
          transition: transform 0.3s ease;
          width: min(100%, 1220px);
          max-height: calc(100dvh - 16px);
        }

        .monitor.powered {
          transform: scale(1);
        }

        .vent-top {
          display: flex;
          justify-content: center;
          gap: 10px;
          margin-bottom: 14px;
        }

        .vent-slot {
          width: 35px;
          height: 5px;
          background: linear-gradient(180deg,
            #a099b8 0%,
            #8880a8 50%,
            #a099b8 100%
          );
          border-radius: 2px;
          box-shadow: inset 0 1px 2px rgba(60, 40, 100, 0.4);
        }

        .monitor-face {
          display: flex;
          flex-direction: column;
          background: linear-gradient(180deg,
            #d8d3e8 0%,
            #cdc8e0 100%
          );
          border-radius: 12px;
          padding: 14px;
          box-shadow:
            inset 0 2px 4px rgba(100, 80, 150, 0.1),
            inset 0 -1px 0 rgba(255, 255, 255, 0.4);
        }

        .screen-bezel {
          flex: none;
          width: 100%;
          aspect-ratio: 4 / 3;
          background: linear-gradient(180deg,
            #2a2535 0%,
            #1a1520 10%,
            #0f0a14 90%,
            #1a1520 100%
          );
          border-radius: 18px;
          padding: 12px;
          box-shadow:
            inset 0 6px 18px rgba(20, 10, 40, 0.8),
            inset 0 -2px 6px rgba(200, 180, 255, 0.05),
            0 2px 0 rgba(255, 255, 255, 0.15);
        }

        .crt-glass {
          position: relative;
          height: 100%;
          border-radius: 12px;
          overflow: hidden;
          background: #000;
          box-shadow:
            inset 0 0 100px rgba(60, 40, 80, 0.3),
            inset 0 0 25px rgba(0, 0, 0, 0.8);
        }

        .screen-content {
          position: relative;
          height: 100%;
          z-index: 1;
          opacity: 0;
          transform: scaleY(0.01);
          transition: all 0.5s ease;
          filter: brightness(1.1) contrast(1.1);
        }

        .screen-content.on {
          opacity: 1;
          transform: scaleY(1);
        }

        .scanlines {
          position: absolute;
          top: 0;
          left: 0;
          right: 0;
          bottom: 0;
          background: repeating-linear-gradient(
            0deg,
            rgba(0, 0, 0, 0) 0px,
            rgba(0, 0, 0, 0) 1px,
            rgba(0, 0, 0, 0.3) 1px,
            rgba(0, 0, 0, 0.3) 2px
          );
          pointer-events: none;
          z-index: 10;
        }

        .rgb-pixels {
          position: absolute;
          top: 0;
          left: 0;
          right: 0;
          bottom: 0;
          background-image:
            repeating-linear-gradient(
              90deg,
              rgba(255, 0, 0, 0.03) 0px,
              rgba(0, 255, 0, 0.03) 1px,
              rgba(0, 0, 255, 0.03) 2px,
              transparent 3px
            );
          pointer-events: none;
          z-index: 11;
          opacity: 0.5;
        }

        .screen-curve {
          position: absolute;
          top: 0;
          left: 0;
          right: 0;
          bottom: 0;
          background: radial-gradient(
            ellipse at center,
            transparent 0%,
            transparent 60%,
            rgba(0, 0, 0, 0.3) 90%,
            rgba(0, 0, 0, 0.6) 100%
          );
          pointer-events: none;
          z-index: 12;
          border-radius: 12px;
        }

        .phosphor-glow {
          position: absolute;
          top: 0;
          left: 0;
          right: 0;
          bottom: 0;
          box-shadow: inset 0 0 120px rgba(180, 160, 255, 0.08);
          pointer-events: none;
          z-index: 5;
          mix-blend-mode: screen;
        }

        .screen-glare {
          position: absolute;
          top: 0;
          left: 0;
          right: 0;
          bottom: 0;
          background:
            linear-gradient(
              135deg,
              rgba(255, 255, 255, 0.1) 0%,
              transparent 40%
            ),
            linear-gradient(
              -135deg,
              rgba(255, 255, 255, 0.03) 0%,
              transparent 30%
            );
          pointer-events: none;
          z-index: 15;
        }

        .flicker {
          position: absolute;
          top: 0;
          left: 0;
          right: 0;
          bottom: 0;
          pointer-events: none;
          z-index: 14;
          animation: flicker 0.15s infinite;
          opacity: 0.03;
          background: white;
        }

        @keyframes flicker {
          0% { opacity: 0.02; }
          5% { opacity: 0.04; }
          10% { opacity: 0.02; }
          15% { opacity: 0.05; }
          20% { opacity: 0.02; }
          100% { opacity: 0.02; }
        }

        .chromatic-left {
          position: absolute;
          top: 0;
          left: 0;
          bottom: 0;
          width: 4px;
          background: linear-gradient(90deg,
            rgba(255, 0, 0, 0.2) 0%,
            transparent 100%
          );
          pointer-events: none;
          z-index: 13;
        }

        .chromatic-right {
          position: absolute;
          top: 0;
          right: 0;
          bottom: 0;
          width: 4px;
          background: linear-gradient(-90deg,
            rgba(0, 0, 255, 0.2) 0%,
            transparent 100%
          );
          pointer-events: none;
          z-index: 13;
        }

        .monitor-badge {
          margin-top: 8px;
          text-align: center;
        }

        .badge-text {
          font-family: 'Helvetica Neue', Arial, sans-serif;
          font-size: 16px;
          font-weight: 300;
          color: #7a7090;
          letter-spacing: 5px;
          text-transform: uppercase;
          text-shadow: 0 1px 0 rgba(255, 255, 255, 0.6);
        }

        .badge-model {
          display: block;
          font-family: 'Helvetica Neue', Arial, sans-serif;
          font-size: 9px;
          color: #9990a8;
          letter-spacing: 2px;
          margin-top: 2px;
        }

        .monitor-base {
          display: flex;
          align-items: center;
          justify-content: space-between;
          margin-top: 8px;
          padding: 8px 16px;
          background: linear-gradient(180deg,
            #cdc8e0 0%,
            #c0bad8 100%
          );
          border-radius: 0 0 10px 10px;
        }

        .floppy-drive {
          display: flex;
          align-items: center;
          gap: 10px;
        }

        .floppy-slot {
          width: 88px;
          height: 6px;
          background: linear-gradient(180deg,
            #4a4060 0%,
            #3a3050 50%,
            #4a4060 100%
          );
          border-radius: 1px;
          box-shadow:
            inset 0 1px 2px rgba(30, 20, 60, 0.5),
            0 1px 0 rgba(255, 255, 255, 0.3);
        }

        .floppy-light {
          width: 7px;
          height: 7px;
          border-radius: 50%;
          background: #3a3050;
          box-shadow: inset 0 1px 2px rgba(30, 20, 60, 0.5);
        }

        .controls {
          display: flex;
          gap: 16px;
        }

        .control-group {
          display: flex;
          flex-direction: column;
          align-items: center;
          gap: 5px;
        }

        .brightness-knob,
        .contrast-knob {
          width: 24px;
          height: 24px;
          border-radius: 50%;
          background: linear-gradient(180deg,
            #8880a0 0%,
            #686088 50%,
            #585078 100%
          );
          box-shadow:
            0 2px 4px rgba(40, 30, 80, 0.3),
            inset 0 1px 0 rgba(255, 255, 255, 0.25);
          position: relative;
          cursor: pointer;
        }

        .knob-indicator {
          position: absolute;
          top: 5px;
          left: 50%;
          transform: translateX(-50%);
          width: 2px;
          height: 7px;
          background: #e0d8f0;
          border-radius: 1px;
        }

        .control-label {
          font-family: 'Helvetica Neue', Arial, sans-serif;
          font-size: 8px;
          color: #7a7090;
          letter-spacing: 1px;
          text-transform: uppercase;
        }

        .power-section {
          display: flex;
          flex-direction: column;
          align-items: center;
          gap: 5px;
        }

        .power-led {
          width: 10px;
          height: 10px;
          border-radius: 50%;
          background: #3a3050;
          box-shadow: inset 0 1px 2px rgba(30, 20, 60, 0.5);
          transition: all 0.3s ease;
        }

        .power-led.on {
          background: #a0e0b0;
          box-shadow:
            0 0 10px rgba(160, 224, 176, 0.8),
            0 0 20px rgba(160, 224, 176, 0.5),
            inset 0 -2px 4px rgba(0, 0, 0, 0.2);
        }

        .power-label {
          font-family: 'Helvetica Neue', Arial, sans-serif;
          font-size: 8px;
          color: #7a7090;
          letter-spacing: 1px;
        }

        .side-vent {
          position: absolute;
          top: 50%;
          transform: translateY(-50%);
          display: flex;
          flex-direction: column;
          gap: 6px;
        }

        .side-vent.left {
          left: 6px;
        }

        .side-vent.right {
          right: 6px;
        }

        .vent-horizontal {
          width: 4px;
          height: 12px;
          background: linear-gradient(90deg,
            #a8a0c0 0%,
            #8880a8 50%,
            #a8a0c0 100%
          );
          border-radius: 1px;
          box-shadow: inset 0 0 2px rgba(60, 40, 100, 0.3);
        }

        @keyframes wobble {
          0%, 100% { transform: translateX(0); }
          25% { transform: translateX(0.5px); }
          75% { transform: translateX(-0.5px); }
        }

        @media (max-width: 900px) {
          .monitor {
            width: min(100%, 960px);
          }
        }

        @media (max-width: 720px) {
          .crt-room {
            padding: 6px;
          }

          .monitor {
            border-radius: 22px 22px 10px 10px;
            padding: 14px 16px 10px 16px;
            max-height: calc(100dvh - 12px);
          }

          .vent-top {
            gap: 6px;
            margin-bottom: 12px;
          }

          .vent-slot {
            width: 24px;
          }

          .monitor-face {
            padding: 12px;
          }

          .screen-bezel {
            padding: 10px;
            aspect-ratio: 3 / 4;
          }

          .monitor-base {
            padding: 8px 12px;
          }

          .floppy-slot {
            width: 72px;
          }

          .controls {
            gap: 12px;
          }

          .brightness-knob,
          .contrast-knob {
            width: 22px;
            height: 22px;
          }

          .badge-text {
            font-size: 15px;
          }
        }
      `}</style>
    </div>
  );
}
