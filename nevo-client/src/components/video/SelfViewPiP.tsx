import { useState, useRef } from "react";
import { User } from "lucide-react";

export default function SelfViewPiP() {
  const [pos, setPos] = useState({ x: 0, y: 0 });
  const [dragging, setDragging] = useState(false);
  const startRef = useRef({ x: 0, y: 0, posX: 0, posY: 0 });

  const onMouseDown = (e: React.MouseEvent) => {
    setDragging(true);
    startRef.current = {
      x: e.clientX,
      y: e.clientY,
      posX: pos.x,
      posY: pos.y,
    };
  };

  const onMouseMove = (e: React.MouseEvent) => {
    if (!dragging) return;
    setPos({
      x: startRef.current.posX + (e.clientX - startRef.current.x),
      y: startRef.current.posY + (e.clientY - startRef.current.y),
    });
  };

  const onMouseUp = () => setDragging(false);

  return (
    <div
      className="absolute z-10 cursor-grab"
      style={{
        bottom: 120,
        right: 32,
        width: 240,
        height: 180,
        borderRadius: 12,
        border: "1px solid var(--color-border)",
        overflow: "hidden",
        boxShadow: "var(--shadow-floating)",
        background:
          "linear-gradient(135deg, var(--color-bg-surface) 0%, var(--color-bg-elevated) 100%)",
        transform: `translate(${pos.x}px, ${pos.y}px) ${dragging ? "scale(1.02)" : ""}`,
        transition: dragging ? "none" : "transform 200ms ease",
      }}
      onMouseDown={onMouseDown}
      onMouseMove={onMouseMove}
      onMouseUp={onMouseUp}
      onMouseLeave={onMouseUp}
      onMouseEnter={(e) => {
        if (!dragging) e.currentTarget.style.transform = `translate(${pos.x}px, ${pos.y}px) scale(1.02)`;
      }}
      onMouseOut={(e) => {
        if (!dragging) e.currentTarget.style.transform = `translate(${pos.x}px, ${pos.y}px) scale(1)`;
      }}
    >
      <div className="w-full h-full flex flex-col items-center justify-center">
        <div
          className="flex items-center justify-center rounded-full"
          style={{
            width: 56,
            height: 56,
            background: "var(--color-bg-overlay)",
          }}
        >
          <User
            size={24}
            style={{ color: "var(--color-text-tertiary)" }}
          />
        </div>
        <span
          className="mt-2"
          style={{
            fontSize: 11,
            color: "var(--color-text-tertiary)",
            fontFamily: "var(--font-body)",
          }}
        >
          你的摄像头
        </span>
      </div>
      <div
        className="absolute rounded"
        style={{
          top: 8,
          left: "50%",
          transform: "translateX(-50%)",
          width: 32,
          height: 3,
          background: "rgba(255,255,255,0.15)",
        }}
      />
    </div>
  );
}
