import { useState, useEffect } from "react";
import { useParams } from "react-router-dom";
import { User as UserIcon, MessageSquare } from "lucide-react";
import { useAppStore } from "@/store/useAppStore";
import CallTopBar from "@/components/video/CallTopBar";
import SelfViewPiP from "@/components/video/SelfViewPiP";
import CallControlBar from "@/components/video/CallControlBar";

export default function VideoCallPage() {
  const { userId } = useParams<{ userId: string }>();
  const { getUser } = useAppStore();
  const caller = getUser(userId ?? "");
  const [seconds, setSeconds] = useState(0);
  const [chatOpen, setChatOpen] = useState(false);

  useEffect(() => {
    const timer = setInterval(() => setSeconds((s) => s + 1), 1000);
    return () => clearInterval(timer);
  }, []);

  const formatTime = (total: number) => {
    const m = Math.floor(total / 60);
    const s = total % 60;
    return `${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}`;
  };

  return (
    <main
      className="relative h-full w-full overflow-hidden"
      style={{ background: "#12141A" }}
    >
      {/* Background gradient */}
      <div
        className="absolute inset-0"
        style={{
          background:
            "linear-gradient(160deg, #1A1B1E 0%, #22242A 40%, #1E2228 70%, #1A1B1E 100%)",
        }}
      />
      <div
        className="absolute inset-0"
        style={{
          opacity: 0.03,
          backgroundImage:
            "radial-gradient(circle at 30% 40%, var(--color-primary) 0%, transparent 50%), radial-gradient(circle at 70% 60%, var(--color-primary) 0%, transparent 40%)",
        }}
      />

      {/* Caller avatar centered (no-video state) */}
      <div className="absolute inset-0 z-[1] flex flex-col items-center justify-center pointer-events-none">
        <div
          className="flex items-center justify-center rounded-full"
          style={{
            width: 96,
            height: 96,
            background: "var(--color-bg-elevated)",
            border: "2px solid var(--color-border)",
          }}
        >
          <UserIcon
            size={40}
            style={{ color: "var(--color-text-secondary)" }}
          />
        </div>
        <p
          className="mt-4 font-semibold"
          style={{
            fontSize: 20,
            color: "var(--color-text-primary)",
            fontFamily: "var(--font-display)",
          }}
        >
          {caller?.name ?? "未知用户"}
        </p>
        <p
          className="mt-1"
          style={{
            fontSize: 13,
            color: "var(--color-text-tertiary)",
            fontFamily: "var(--font-body)",
          }}
        >
          对方摄像头已关闭
        </p>
      </div>

      <CallTopBar
        callerName={caller?.name ?? "未知"}
        duration={formatTime(seconds)}
      />

      <SelfViewPiP />

      <CallControlBar onHangup={() => setSeconds(0)} />

      {/* Chat toggle button */}
      <button
        onClick={() => setChatOpen((v) => !v)}
        className="absolute z-10 transition-colors duration-fast"
        style={{
          right: 0,
          top: "50%",
          transform: "translateY(-50%)",
          width: 40,
          height: 80,
          background: chatOpen
            ? "rgba(34,36,42,0.8)"
            : "rgba(34,36,42,0.5)",
          backdropFilter: "blur(12px)",
          WebkitBackdropFilter: "blur(12px)",
          border: "1px solid var(--color-border)",
          borderRight: "none",
          borderRadius: "8px 0 0 8px",
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
          cursor: "pointer",
        }}
        aria-label="通话聊天"
      >
        <MessageSquare
          size={20}
          style={{ color: "var(--color-text-secondary)" }}
        />
      </button>
    </main>
  );
}
