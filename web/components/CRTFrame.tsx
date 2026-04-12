"use client";

import { type ReactNode, useEffect, useRef, useState } from "react";

interface CRTFrameProps {
  children: ReactNode;
}

// ASCII characters for the swirl effect (from dense to sparse)
const ASCII_CHARS = "█▓▒░@%#*+=-:. ";

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
          const alpha = 0.32 + intensity * 0.28;

          ctx.fillStyle = `hsla(${hue}, ${saturation}%, ${lightness}%, ${alpha})`;
          ctx.fillText(char, gridX, gridY);
        }
      }

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
      <canvas ref={canvasRef} className="ascii-bg" />

      <div className={`glass-stage ${powerOn ? "powered" : ""}`}>
        <div className={`screen-content ${powerOn ? "on" : ""}`}>{children}</div>
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
          opacity: 0.42;
          filter: blur(0.6px);
        }

        .glass-stage {
          position: relative;
          z-index: 2;
          width: min(100%, 1360px);
          max-height: calc(100dvh - 24px);
          padding: 0;
          transform: translateY(10px) scale(0.985);
          opacity: 0.92;
          transition: transform 0.45s ease, opacity 0.45s ease;
        }

        .glass-stage.powered {
          transform: translateY(0) scale(1);
          opacity: 1;
        }

        .screen-content {
          position: relative;
          width: 100%;
          height: min(86dvh, 920px);
          z-index: 1;
          opacity: 0;
          transform: scale(0.985);
          transition: all 0.5s ease;
          filter: brightness(1.01) contrast(1.01);
        }

        .screen-content.on {
          opacity: 1;
          transform: scale(1);
        }

        @media (max-width: 900px) {
          .glass-stage {
            width: min(100%, 1040px);
          }

          .screen-content {
            height: min(84dvh, 860px);
          }
        }

        @media (max-width: 720px) {
          .crt-room {
            height: auto;
            min-height: 100dvh;
            justify-content: flex-start;
            padding: 6px;
            overflow-y: auto;
            overflow-x: hidden;
          }

          .glass-stage {
            width: 100%;
            max-height: none;
          }

          .screen-content {
            height: auto;
            min-height: calc(100dvh - 24px);
          }

          .ambient-glow {
            width: 72vw;
            height: 72vw;
          }
        }
      `}</style>
    </div>
  );
}
