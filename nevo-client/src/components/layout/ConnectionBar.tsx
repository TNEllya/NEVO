import { useNavigate } from "react-router-dom";
import { useVoiceStore } from "@/store/useVoiceStore";
import { Mic, MicOff, Headphones, Settings } from "lucide-react";

export default function ConnectionBar() {
  const navigate = useNavigate();
  const { selfMuted, deafened, toggleSelfMute, toggleDeafen } = useVoiceStore();

  return (
    <div
      className="flex items-center gap-1.5 px-2 shrink-0"
      style={{
        height: 52,
        minHeight: 52,
        background: "var(--color-bg-base)",
        borderTop: "1px solid var(--color-border)",
      }}
    >
      {/* Self avatar */}
      <div
        className="relative cursor-pointer"
        style={{ width: 32, height: 32 }}
      >
        <div
          className="flex items-center justify-center rounded-full"
          style={{
            width: 32,
            height: 32,
            background: "var(--color-primary)",
          }}
        >
          <span
            className="font-bold"
            style={{ fontSize: 14, color: "var(--color-primary-foreground)" }}
          >
            我
          </span>
        </div>
        <div
          className="absolute rounded-full border-2"
          style={{
            bottom: -1,
            right: -1,
            width: 12,
            height: 12,
            background: selfMuted ? "var(--state-error)" : "var(--color-primary)",
            borderColor: "var(--color-bg-base)",
          }}
        />
      </div>

      <div className="flex items-center gap-1 px-1 flex-1">
        <button
          onClick={toggleSelfMute}
          className="p-1 rounded-sm transition-colors duration-fast"
          style={{
            background: selfMuted ? "var(--state-error)" : "transparent",
          }}
          aria-label="麦克风"
        >
          {selfMuted ? (
            <MicOff size={16} className="text-white" />
          ) : (
            <Mic
              size={16}
              style={{ color: "var(--color-text-secondary)" }}
            />
          )}
        </button>
        <button
          onClick={toggleDeafen}
          className="p-1 rounded-sm transition-colors duration-fast"
          style={{
            background: deafened ? "var(--state-error)" : "transparent",
          }}
          aria-label="耳机"
        >
          <Headphones
            size={16}
            className={deafened ? "text-white" : ""}
            style={
              !deafened ? { color: "var(--color-text-secondary)" } : undefined
            }
          />
        </button>
      </div>

      <button
        onClick={() => navigate("/settings/audio")}
        className="p-1 rounded-sm cursor-pointer transition-colors duration-fast"
        onMouseEnter={(e) =>
          (e.currentTarget.style.background = "var(--color-bg-hover)")
        }
        onMouseLeave={(e) => (e.currentTarget.style.background = "transparent")}
        aria-label="设置"
      >
        <Settings
          size={16}
          style={{ color: "var(--color-text-secondary)" }}
        />
      </button>
    </div>
  );
}
