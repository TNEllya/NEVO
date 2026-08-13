import { useNavigate } from "react-router-dom";
import {
  User,
  Mic,
  Video,
  Bell,
  Keyboard,
  Info,
} from "lucide-react";
import type { SettingsSection } from "@/types";

interface SettingsNavProps {
  active: SettingsSection;
}

const NAV_ITEMS: { id: SettingsSection; label: string; icon: React.ReactNode }[] = [
  { id: "account", label: "账户", icon: <User size={18} /> },
  { id: "audio", label: "音频设置", icon: <Mic size={18} /> },
  { id: "video", label: "视频设置", icon: <Video size={18} /> },
  { id: "notifications", label: "通知", icon: <Bell size={18} /> },
  { id: "hotkeys", label: "快捷键", icon: <Keyboard size={18} /> },
  { id: "about", label: "关于", icon: <Info size={18} /> },
];

export default function SettingsNav({ active }: SettingsNavProps) {
  const navigate = useNavigate();

  return (
    <nav
      className="hidden md:flex flex-col gap-0.5 shrink-0 p-4"
      style={{
        width: 200,
        minWidth: 200,
        borderRight: "1px solid var(--color-border)",
        background: "var(--color-bg-surface)",
      }}
    >
      {NAV_ITEMS.map((item) => {
        const isActive = item.id === active;
        return (
          <a
            key={item.id}
            onClick={() => navigate(`/settings/${item.id}`)}
            className="flex items-center gap-2.5 px-3 py-2.5 rounded-md cursor-pointer transition-colors duration-fast"
            style={{
              background: isActive ? "var(--color-primary-muted)" : "transparent",
              color: isActive
                ? "var(--color-primary)"
                : "var(--color-text-secondary)",
              fontSize: 14,
              fontWeight: isActive ? 500 : 400,
            }}
            onMouseEnter={(e) => {
              if (!isActive)
                e.currentTarget.style.background = "var(--color-bg-hover)";
            }}
            onMouseLeave={(e) => {
              if (!isActive) e.currentTarget.style.background = "transparent";
            }}
          >
            {item.icon}
            {item.label}
          </a>
        );
      })}
    </nav>
  );
}

export { NAV_ITEMS };
