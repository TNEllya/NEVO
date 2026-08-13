import { useNavigate } from "react-router-dom";
import { ChevronLeft, SignalHigh, Lock } from "lucide-react";

interface CallTopBarProps {
  callerName: string;
  duration: string;
}

export default function CallTopBar({ callerName, duration }: CallTopBarProps) {
  const navigate = useNavigate();

  return (
    <header
      className="absolute top-0 left-0 right-0 z-10 flex items-center justify-between px-8 py-5"
      style={{
        background:
          "linear-gradient(180deg, rgba(18,20,26,0.7) 0%, transparent 100%)",
      }}
    >
      <a
        onClick={() => navigate("/")}
        className="inline-flex items-center gap-2 cursor-pointer transition-colors duration-fast"
        style={{
          color: "var(--color-text-secondary)",
          fontSize: 14,
          fontFamily: "var(--font-body)",
          padding: "8px 12px",
          borderRadius: 8,
        }}
        onMouseEnter={(e) =>
          (e.currentTarget.style.background = "rgba(255,255,255,0.08)")
        }
        onMouseLeave={(e) => (e.currentTarget.style.background = "transparent")}
      >
        <ChevronLeft size={20} />
        <span>返回</span>
      </a>

      <div className="flex items-center gap-3">
        <span
          className="font-semibold"
          style={{
            fontSize: 17,
            color: "var(--color-text-primary)",
            fontFamily: "var(--font-display)",
          }}
        >
          {callerName}
        </span>
        <span
          className="animate-pulse-dot"
          style={{
            fontSize: 13,
            color: "var(--color-text-secondary)",
            fontFamily: "var(--font-mono)",
            fontVariantNumeric: "tabular-nums",
          }}
        >
          {duration}
        </span>
        <span
          className="rounded-full animate-pulse-dot"
          style={{
            width: 6,
            height: 6,
            background: "var(--color-primary)",
            display: "inline-block",
          }}
        />
      </div>

      <div className="flex items-center gap-4">
        <div className="flex items-center gap-1.5" title="连接质量：良好">
          <SignalHigh size={18} style={{ color: "var(--color-primary)" }} />
          <span
            style={{
              fontSize: 11,
              color: "var(--color-text-tertiary)",
              fontFamily: "var(--font-body)",
            }}
          >
            良好
          </span>
        </div>
        <div className="flex items-center gap-1.5" title="端到端加密">
          <Lock
            size={16}
            style={{ color: "var(--color-text-tertiary)" }}
          />
        </div>
      </div>
    </header>
  );
}
