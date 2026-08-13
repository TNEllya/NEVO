import { useState } from "react";
import { useAppStore } from "@/store/useAppStore";
import { useVoiceStore } from "@/store/useVoiceStore";
import Avatar from "@/components/shared/Avatar";
import ConnectionBar from "./ConnectionBar";
import { ChevronDown, Hash, Volume2, ChevronRight } from "lucide-react";
import { cn } from "@/lib/utils";

export default function ChannelSidebar() {
  const { servers, currentServerId, channels, currentChannelId, setChannel, getUser } =
    useAppStore();
  const { getVoiceMembersByChannel } = useVoiceStore();
  const server = servers.find((s) => s.id === currentServerId);
  const serverChannels = channels.filter((c) => c.serverId === currentServerId);

  const categories = Array.from(new Set(serverChannels.map((c) => c.category)));
  const [collapsed, setCollapsed] = useState<Record<string, boolean>>({});

  const toggleCategory = (cat: string) =>
    setCollapsed((p) => ({ ...p, [cat]: !p[cat] }));

  return (
    <aside
      className="flex flex-col shrink-0"
      style={{ width: 240, minWidth: 240, background: "var(--color-bg-surface)" }}
      aria-label="频道列表"
    >
      {/* Server name header */}
      <div
        className="flex items-center justify-between px-4 cursor-pointer transition-colors duration-fast shrink-0"
        style={{
          height: 48,
          borderBottom: "1px solid var(--color-border)",
        }}
        onMouseEnter={(e) =>
          (e.currentTarget.style.background = "var(--color-bg-hover)")
        }
        onMouseLeave={(e) => (e.currentTarget.style.background = "transparent")}
      >
        <span
          className="font-semibold"
          style={{ fontSize: 15, color: "var(--color-text-primary)" }}
        >
          {server?.name}
        </span>
        <ChevronDown size={14} style={{ color: "var(--color-text-secondary)" }} />
      </div>

      {/* Channel tree */}
      <div className="flex-1 overflow-y-auto no-scrollbar py-2">
        {categories.map((cat) => {
          const isCollapsed = collapsed[cat];
          const catChannels = serverChannels.filter((c) => c.category === cat);
          return (
            <div key={cat} className="mb-1">
              <div
                className="flex items-center gap-1 px-2 py-1 cursor-pointer"
                onClick={() => toggleCategory(cat)}
              >
                {isCollapsed ? (
                  <ChevronRight
                    size={10}
                    style={{ color: "var(--color-text-secondary)" }}
                  />
                ) : (
                  <ChevronDown
                    size={10}
                    style={{ color: "var(--color-text-secondary)" }}
                  />
                )}
                <span
                  className="font-semibold uppercase tracking-wide"
                  style={{
                    fontSize: 11,
                    color: "var(--color-text-tertiary)",
                    letterSpacing: "0.05em",
                  }}
                >
                  {cat}
                </span>
              </div>

              {!isCollapsed &&
                catChannels.map((channel) => {
                  const active = channel.id === currentChannelId;
                  const isVoice = channel.type === "voice";
                  const members = isVoice ? getVoiceMembersByChannel(channel.id) : [];
                  return (
                    <div key={channel.id} className="mx-2 my-px">
                      <div
                        className={cn(
                          "flex items-center gap-2 px-3 py-1.5 cursor-pointer rounded-sm transition-colors duration-fast"
                        )}
                        style={{
                          background: active
                            ? "var(--color-bg-hover)"
                            : "transparent",
                        }}
                        onMouseEnter={(e) => {
                          if (!active)
                            e.currentTarget.style.background =
                              "var(--color-bg-hover)";
                        }}
                        onMouseLeave={(e) => {
                          if (!active)
                            e.currentTarget.style.background = "transparent";
                        }}
                        onClick={() => setChannel(channel.id)}
                      >
                        {isVoice ? (
                          <Volume2
                            size={18}
                            style={{ color: "var(--color-text-tertiary)" }}
                          />
                        ) : (
                          <Hash
                            size={18}
                            style={{ color: "var(--color-text-tertiary)" }}
                          />
                        )}
                        <span
                          className="font-medium"
                          style={{
                            fontSize: 14,
                            color: active
                              ? "var(--color-text-primary)"
                              : "var(--color-text-secondary)",
                          }}
                        >
                          {channel.name}
                        </span>
                        {channel.unread ? (
                          <div
                            className="flex items-center justify-center ml-auto rounded-full"
                            style={{
                              width: 16,
                              height: 16,
                              background: "var(--state-error)",
                            }}
                          >
                            <span
                              className="font-bold text-white"
                              style={{ fontSize: 10 }}
                            >
                              {channel.unread}
                            </span>
                          </div>
                        ) : isVoice && members.length > 0 ? (
                          <span
                            className="ml-auto"
                            style={{
                              fontSize: 11,
                              color: "var(--color-text-tertiary)",
                            }}
                          >
                            {members.length}
                          </span>
                        ) : null}
                      </div>

                      {/* Voice channel connected users */}
                      {isVoice && members.length > 0 && (
                        <div className="pl-5 mt-0.5">
                          {members.map((vm) => {
                            const u = getUser(vm.userId);
                            const statusColor =
                              vm.voiceState === "muted"
                                ? "var(--state-error)"
                                : vm.voiceState === "speaking"
                                ? "var(--color-primary)"
                                : undefined;
                            return (
                              <div
                                key={vm.id}
                                className="flex items-center gap-2 px-2 py-1 rounded-sm cursor-pointer transition-colors duration-fast"
                                onMouseEnter={(e) =>
                                  (e.currentTarget.style.background =
                                    "var(--color-bg-hover)")
                                }
                                onMouseLeave={(e) =>
                                  (e.currentTarget.style.background =
                                    "transparent")
                                }
                              >
                                <Avatar
                                  user={u}
                                  size={24}
                                  showStatus={vm.voiceState !== "idle"}
                                  statusColor={statusColor}
                                />
                                <span
                                  className="font-medium"
                                  style={{
                                    fontSize: 13,
                                    color:
                                      vm.voiceState === "muted"
                                        ? "var(--color-text-tertiary)"
                                        : vm.voiceState === "speaking"
                                        ? "var(--color-text-primary)"
                                        : "var(--color-text-secondary)",
                                  }}
                                >
                                  {u?.name}
                                </span>
                              </div>
                            );
                          })}
                        </div>
                      )}
                    </div>
                  );
                })}
            </div>
          );
        })}
      </div>

      <ConnectionBar />
    </aside>
  );
}
