import { useAppStore } from "@/store/useAppStore";
import { cn } from "@/lib/utils";
import { Plus } from "lucide-react";

export default function ServerSidebar() {
  const { servers, currentServerId, setServer } = useAppStore();

  return (
    <nav
      className="hidden md:flex flex-col items-center py-3 gap-1 overflow-y-auto no-scrollbar shrink-0"
      style={{ width: 72, minWidth: 72, background: "var(--color-bg-base)" }}
      aria-label="服务器列表"
    >
      {/* NEVO Logo */}
      <div
        className="flex items-center justify-center mb-2 cursor-pointer transition-all duration-fast"
        style={{
          width: 48,
          height: 48,
          borderRadius: 8,
          background: "var(--color-primary)",
        }}
        onMouseEnter={(e) => (e.currentTarget.style.borderRadius = "12px")}
        onMouseLeave={(e) => (e.currentTarget.style.borderRadius = "8px")}
      >
        <span
          className="font-bold"
          style={{ fontSize: 22, color: "var(--color-primary-foreground)" }}
        >
          N
        </span>
      </div>

      <div
        className="rounded mb-2"
        style={{ width: 32, height: 2, background: "var(--color-border)" }}
      />

      {servers.map((server) => {
        const active = server.id === currentServerId;
        return (
          <div
            key={server.id}
            className="relative flex items-center cursor-pointer"
            onClick={() => setServer(server.id)}
          >
            {active && (
              <div
                className="absolute rounded-r"
                style={{
                  left: -12,
                  width: 4,
                  height: 40,
                  background: "var(--color-primary)",
                }}
              />
            )}
            <div
              className={cn(
                "flex items-center justify-center transition-all duration-fast"
              )}
              style={{
                width: 48,
                height: 48,
                borderRadius: active ? 12 : 8,
                background: "var(--color-bg-surface)",
                border: active ? "2px solid var(--color-primary)" : "none",
              }}
              onMouseEnter={(e) =>
                (e.currentTarget.style.borderRadius = "12px")
              }
              onMouseLeave={(e) =>
                (e.currentTarget.style.borderRadius = active ? "12px" : "8px")
              }
            >
              <span
                className="font-semibold"
                style={{
                  fontSize: 16,
                  color: active
                    ? "var(--color-text-primary)"
                    : "var(--color-text-secondary)",
                }}
              >
                {server.abbr}
              </span>
            </div>
          </div>
        );
      })}

      <div className="flex-1" />

      {/* Add server */}
      <div
        className="flex items-center justify-center cursor-pointer transition-all duration-fast"
        style={{
          width: 48,
          height: 48,
          borderRadius: 8,
          background: "var(--color-bg-surface)",
        }}
        onMouseEnter={(e) => {
          e.currentTarget.style.background = "var(--color-primary)";
          e.currentTarget.style.borderRadius = "12px";
        }}
        onMouseLeave={(e) => {
          e.currentTarget.style.background = "var(--color-bg-surface)";
          e.currentTarget.style.borderRadius = "8px";
        }}
      >
        <Plus
          size={20}
          className="transition-colors duration-fast"
          style={{ color: "var(--color-text-secondary)" }}
        />
      </div>
    </nav>
  );
}
