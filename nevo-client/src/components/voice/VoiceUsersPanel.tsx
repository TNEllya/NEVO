import { useState } from "react";
import { useVoiceStore } from "@/store/useVoiceStore";
import VoiceUserItem from "./VoiceUserItem";
import { ShieldCheck, ChevronRight } from "lucide-react";

interface VoiceUsersPanelProps {
  channelId: string;
}

export default function VoiceUsersPanel({ channelId }: VoiceUsersPanelProps) {
  const { getVoiceMembersByChannel, connected, latency } = useVoiceStore();
  const members = getVoiceMembersByChannel(channelId);
  const [collapsed, setCollapsed] = useState(false);

  if (collapsed) {
    return (
      <button
        onClick={() => setCollapsed(false)}
        className="hidden lg:flex items-center justify-center shrink-0 border-l"
        style={{
          width: 32,
          background: "var(--color-bg-surface)",
          borderColor: "var(--color-border)",
        }}
        aria-label="展开语音面板"
      >
        <ChevronRight size={18} style={{ color: "var(--color-text-secondary)" }} />
      </button>
    );
  }

  return (
    <aside
      className="hidden lg:flex flex-col shrink-0 border-l"
      style={{
        width: 280,
        minWidth: 280,
        background: "var(--color-bg-surface)",
        borderColor: "var(--color-border)",
      }}
      aria-label="语音用户列表"
    >
      {/* Header */}
      <div
        className="flex items-center justify-between px-4 shrink-0"
        style={{
          height: 48,
          minHeight: 48,
          borderBottom: "1px solid var(--color-border)",
        }}
      >
        <span
          className="font-semibold uppercase"
          style={{
            fontSize: 13,
            color: "var(--color-text-secondary)",
            letterSpacing: "0.03em",
          }}
        >
          语音连接
        </span>
        <span style={{ fontSize: 11, color: "var(--color-text-tertiary)" }}>
          {members.length} 人在线
        </span>
      </div>

      {/* Users */}
      <div className="flex-1 overflow-y-auto no-scrollbar p-2">
        {members.map((member) => (
          <VoiceUserItem key={member.id} member={member} />
        ))}
      </div>

      {/* Footer: connection info */}
      <div
        className="px-4 py-3"
        style={{
          borderTop: "1px solid var(--color-border)",
          background: "var(--color-bg-base)",
        }}
      >
        <div className="flex items-center gap-1.5">
          <div
            className="rounded-full"
            style={{
              width: 8,
              height: 8,
              background: connected ? "var(--color-primary)" : "var(--state-error)",
              boxShadow: connected ? "0 0 6px var(--color-primary)" : "none",
            }}
          />
          <span style={{ fontSize: 11, color: "var(--color-text-secondary)" }}>
            {connected ? "已连接" : "未连接"}
          </span>
          <span
            className="ml-auto"
            style={{ fontSize: 11, color: "var(--color-text-tertiary)" }}
          >
            延迟: {latency}ms
          </span>
        </div>
        <div className="flex items-center gap-1.5 mt-1">
          <ShieldCheck
            size={12}
            style={{ color: "var(--color-text-tertiary)" }}
          />
          <span style={{ fontSize: 11, color: "var(--color-text-tertiary)" }}>
            加密: AES-256
          </span>
        </div>
      </div>
    </aside>
  );
}
