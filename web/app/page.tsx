"use client";

import dynamic from "next/dynamic";

const Emulator = dynamic(() => import("@/components/Emulator"), {
  ssr: false,
  loading: () => (
    <div className="w-screen h-screen bg-black flex flex-col items-center justify-center text-white">
      <div className="text-xl mb-2">Loading...</div>
    </div>
  ),
});

export default function Home() {
  return <Emulator />;
}
