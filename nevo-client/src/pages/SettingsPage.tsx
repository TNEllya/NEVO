import { useParams, useNavigate } from "react-router-dom";
import { ArrowLeft } from "lucide-react";
import type { SettingsSection } from "@/types";
import SettingsNav, { NAV_ITEMS } from "@/components/settings/SettingsNav";
import AudioSettings from "@/components/settings/AudioSettings";

const SECTION_TITLES: Record<SettingsSection, string> = {
  account: "账户",
  audio: "音频设置",
  video: "视频设置",
  notifications: "通知",
  hotkeys: "快捷键",
  about: "关于",
};

const APP_VERSION = "v2.1.0";

export default function SettingsPage() {
  const { section } = useParams<{ section: string }>();
  const navigate = useNavigate();
  const active: SettingsSection = (section as SettingsSection) || "audio";
  const isValidSection = NAV_ITEMS.some((i) => i.id === active);
  const current = isValidSection ? active : "audio";

  return (
    <main
      className="h-full w-full flex flex-col overflow-hidden"
      style={{ background: "var(--color-bg-base)" }}
    >
      {/* Top bar */}
      <header
        className="flex items-center gap-4 shrink-0 px-6"
        style={{
          height: 56,
          borderBottom: "1px solid var(--color-border)",
          background: "var(--color-bg-surface)",
        }}
      >
        <button
          onClick={() => navigate("/")}
          className="flex items-center gap-2 transition-colors duration-fast"
          style={{
            background: "transparent",
            border: "none",
            color: "var(--color-text-secondary)",
            fontSize: 14,
            fontFamily: "var(--font-body)",
            cursor: "pointer",
          }}
          onMouseEnter={(e) =>
            (e.currentTarget.style.color = "var(--color-text-primary)")
          }
          onMouseLeave={(e) =>
            (e.currentTarget.style.color = "var(--color-text-secondary)")
          }
        >
          <ArrowLeft size={18} />
          <span className="hidden sm:inline">返回</span>
        </button>
        <div
          className="h-4"
          style={{ width: 1, background: "var(--color-border)" }}
        />
        <h1
          style={{
            fontSize: 15,
            fontWeight: 600,
            color: "var(--color-text-primary)",
            fontFamily: "var(--font-body)",
          }}
        >
          {SECTION_TITLES[current]}
        </h1>
        <div className="flex-1" />
        <span
          style={{
            fontSize: 12,
            color: "var(--color-text-tertiary)",
            fontFamily: "var(--font-mono)",
          }}
        >
          NEVO {APP_VERSION}
        </span>
      </header>

      {/* Mobile horizontal nav */}
      <div
        className="md:hidden flex items-center gap-1 overflow-x-auto no-scrollbar shrink-0 px-3"
        style={{
          height: 44,
          borderBottom: "1px solid var(--color-border)",
          background: "var(--color-bg-surface)",
        }}
      >
        {NAV_ITEMS.map((item) => {
          const isActive = item.id === current;
          return (
            <button
              key={item.id}
              onClick={() => navigate(`/settings/${item.id}`)}
              className="flex items-center gap-1.5 px-3 py-1.5 rounded-md whitespace-nowrap transition-colors duration-fast"
              style={{
                background: isActive
                  ? "var(--color-primary-muted)"
                  : "transparent",
                color: isActive
                  ? "var(--color-primary)"
                  : "var(--color-text-secondary)",
                fontSize: 13,
                fontWeight: isActive ? 500 : 400,
                border: "none",
                cursor: "pointer",
              }}
            >
              {item.icon}
              {item.label}
            </button>
          );
        })}
      </div>

      {/* Body */}
      <div className="flex-1 flex overflow-hidden">
        <SettingsNav active={current} />
        <div className="flex-1 overflow-hidden">
          {current === "audio" ? (
            <AudioSettings />
          ) : (
            <PlaceholderSection section={current} />
          )}
        </div>
      </div>
    </main>
  );
}

function PlaceholderSection({ section }: { section: SettingsSection }) {
  const messages: Record<SettingsSection, { title: string; desc: string }> = {
    account: {
      title: "账户设置",
      desc: "管理你的账户信息、昵称、头像与密码。",
    },
    audio: {
      title: "音频设置",
      desc: "配置麦克风、扬声器与语音活动检测。",
    },
    video: {
      title: "视频设置",
      desc: "选择摄像头、调整分辨率与帧率。",
    },
    notifications: {
      title: "通知",
      desc: "管理消息提醒、提及与频道通知偏好。",
    },
    hotkeys: {
      title: "快捷键",
      desc: "查看并自定义键盘快捷键绑定。",
    },
    about: {
      title: "关于 NEVO",
      desc: `当前版本：${APP_VERSION}`,
    },
  };
  const m = messages[section];

  return (
    <div
      className="h-full overflow-y-auto p-8"
      style={{ background: "var(--color-bg-base)" }}
    >
      <h2
        className="font-semibold mb-2"
        style={{ fontSize: 17, color: "var(--color-text-primary)" }}
      >
        {m.title}
      </h2>
      <p
        style={{
          fontSize: 13,
          color: "var(--color-text-secondary)",
          maxWidth: 480,
        }}
      >
        {m.desc}
      </p>
      <div
        className="mt-6 inline-flex items-center px-3 py-1.5 rounded"
        style={{
          background: "var(--color-primary-muted)",
          color: "var(--color-primary)",
          fontSize: 12,
        }}
      >
        即将推出
      </div>
    </div>
  );
}
