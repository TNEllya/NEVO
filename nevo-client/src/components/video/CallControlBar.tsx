import { useNavigate } from "react-router-dom";
import { Mic, MicOff, Video, VideoOff, ScreenShare, PhoneOff } from "lucide-react";
import { useState } from "react";

interface CallControlBarProps {
  onHangup: () => void;
}

export default function CallControlBar({ onHangup }: CallControlBarProps) {
  const navigate = useNavigate();
  const [micOn, setMicOn] = useState(true);
  const [camOn, setCamOn] = useState(true);
  const [sharing, setSharing] = useState(false);

  return (
    <div className="absolute bottom-8 left-1/2 z-10" style={{ transform: "translateX(-50%)" }}>
      <div
        className="flex items-center gap-2 px-7 py-4 rounded-lg"
        style={{
          background: "rgba(34, 36, 42, 0.65)",
          backdropFilter: "blur(20px)",
          WebkitBackdropFilter: "blur(20px)",
          border: "1px solid rgba(255,255,255,0.06)",
        }}
      >
        <ControlButton
          label="麦克风"
          active={micOn}
          onClick={() => setMicOn((v) => !v)}
        >
          {micOn ? <Mic size={22} /> : <MicOff size={22} />}
        </ControlButton>

        <ControlButton
          label="摄像头"
          active={camOn}
          onClick={() => setCamOn((v) => !v)}
        >
          {camOn ? <Video size={22} /> : <VideoOff size={22} />}
        </ControlButton>

        <ControlButton
          label="共享屏幕"
          active={sharing}
          onClick={() => setSharing((v) => !v)}
        >
          <ScreenShare size={20} />
        </ControlButton>

        <div
          className="mx-1"
          style={{ width: 1, height: 56, background: "var(--color-border)" }}
        />

        <button
          onClick={() => {
            onHangup();
            navigate("/");
          }}
          className="flex flex-col items-center gap-1.5 bg-transparent border-none cursor-pointer p-2 rounded-md transition-all duration-fast"
          style={{ transform: "scale(1)" }}
          onMouseEnter={(e) => {
            e.currentTarget.style.background = "rgba(248,113,113,0.1)";
          }}
          onMouseLeave={(e) => (e.currentTarget.style.background = "transparent")}
          onMouseDown={(e) => (e.currentTarget.style.transform = "scale(0.95)")}
          onMouseUp={(e) => (e.currentTarget.style.transform = "scale(1)")}
          aria-label="结束通话"
        >
          <div
            className="flex items-center justify-center rounded-full"
            style={{
              width: 56,
              height: 56,
              background: "var(--state-error)",
              boxShadow: "0 4px 16px rgba(248,113,113,0.3)",
            }}
          >
            <PhoneOff
              size={22}
              style={{ color: "var(--color-text-inverse)" }}
            />
          </div>
          <span
            className="whitespace-nowrap"
            style={{
              fontSize: 11,
              color: "var(--color-text-secondary)",
              fontFamily: "var(--font-body)",
            }}
          >
            结束通话
          </span>
        </button>
      </div>
    </div>
  );
}

function ControlButton({
  children,
  label,
  active,
  onClick,
}: {
  children: React.ReactNode;
  label: string;
  active: boolean;
  onClick: () => void;
}) {
  return (
    <button
      onClick={onClick}
      className="flex flex-col items-center gap-1.5 bg-transparent border-none cursor-pointer p-2 rounded-md transition-all duration-fast"
      onMouseEnter={(e) => {
        e.currentTarget.style.background = "rgba(255,255,255,0.08)";
      }}
      onMouseLeave={(e) => (e.currentTarget.style.background = "transparent")}
      onMouseDown={(e) => (e.currentTarget.style.transform = "scale(0.95)")}
      onMouseUp={(e) => (e.currentTarget.style.transform = "scale(1)")}
      aria-label={label}
    >
      <div
        className="flex items-center justify-center rounded-full transition-colors duration-fast"
        style={{
          width: 48,
          height: 48,
          background: "var(--color-bg-elevated)",
          border: active
            ? "2px solid var(--color-primary)"
            : "2px solid transparent",
        }}
      >
        <span style={{ color: active ? "var(--color-primary)" : "var(--color-text-secondary)" }}>
          {children}
        </span>
      </div>
      <span
        className="whitespace-nowrap"
        style={{
          fontSize: 11,
          color: "var(--color-text-secondary)",
          fontFamily: "var(--font-body)",
        }}
      >
        {label}
      </span>
    </button>
  );
}
