import { useState } from "react";
import { Volume2, Hash, Bell, Users, Search, Inbox, Sun, Moon } from "lucide-react";
import { useAppStore } from "@/store/useAppStore";
import { useTheme } from "@/hooks/useTheme";
import type { Channel } from "@/types";

interface MainHeaderProps {
  channel: Channel | undefined;
}

export default function MainHeader({ channel }: MainHeaderProps) {
  const { isDark, toggleTheme } = useTheme();
  const [showMembers, setShowMembers] = useState(false);
  const isVoice = channel?.type === "voice";

  return (
    <header
      className="flex items-center px-4 shrink-0"
      style={{
        height: 48,
        minHeight: 48,
        borderBottom: "1px solid var(--color-border)",
      }}
    >
      {isVoice ? (
        <Volume2
          size={18}
          style={{ color: "var(--color-text-tertiary)", marginRight: 8 }}
        />
      ) : (
        <Hash
          size={18}
          style={{ color: "var(--color-text-tertiary)", marginRight: 8 }}
        />
      )}
      <span
        className="font-semibold"
        style={{ fontSize: 15, color: "var(--color-text-primary)", marginRight: 8 }}
      >
        {channel?.name}
      </span>
      <div
        className="mr-3"
        style={{ width: 1, height: 24, background: "var(--color-border)" }}
      />
      <span style={{ fontSize: 13, color: "var(--color-text-tertiary)" }}>
        {channel?.topic}
      </span>

      <div className="flex-1" />

      <div className="flex items-center gap-0.5">
        <HeaderIconButton aria-label="通知">
          <Bell size={18} style={{ color: "var(--color-text-secondary)" }} />
        </HeaderIconButton>
        <button
          onClick={() => setShowMembers((v) => !v)}
          className="p-1.5 rounded-sm transition-colors duration-fast lg:hidden"
          style={{
            background: showMembers ? "var(--color-bg-hover)" : "transparent",
          }}
          aria-label="成员"
        >
          <Users
            size={18}
            style={{ color: "var(--color-text-secondary)" }}
          />
        </button>
        <HeaderIconButton aria-label="搜索">
          <Search size={18} style={{ color: "var(--color-text-secondary)" }} />
        </HeaderIconButton>
        <HeaderIconButton aria-label="收件箱">
          <Inbox size={18} style={{ color: "var(--color-text-secondary)" }} />
        </HeaderIconButton>
        <HeaderIconButton aria-label="主题" onClick={toggleTheme}>
          {isDark ? (
            <Sun size={18} style={{ color: "var(--color-text-secondary)" }} />
          ) : (
            <Moon size={18} style={{ color: "var(--color-text-secondary)" }} />
          )}
        </HeaderIconButton>
      </div>
    </header>
  );
}

function HeaderIconButton({
  children,
  onClick,
  ...props
}: React.ButtonHTMLAttributes<HTMLButtonElement>) {
  return (
    <button
      onClick={onClick}
      className="p-1.5 rounded-sm cursor-pointer transition-colors duration-fast"
      onMouseEnter={(e) =>
        (e.currentTarget.style.background = "var(--color-bg-hover)")
      }
      onMouseLeave={(e) => (e.currentTarget.style.background = "transparent")}
      {...props}
    >
      {children}
    </button>
  );
}
