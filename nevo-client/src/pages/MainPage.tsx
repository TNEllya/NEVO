import { useAppStore } from "@/store/useAppStore";
import ServerSidebar from "@/components/layout/ServerSidebar";
import ChannelSidebar from "@/components/layout/ChannelSidebar";
import MainHeader from "@/components/layout/MainHeader";
import ChatPanel from "@/components/chat/ChatPanel";
import VoiceUsersPanel from "@/components/voice/VoiceUsersPanel";

export default function MainPage() {
  const { currentChannelId, getChannel } = useAppStore();
  const channel = getChannel(currentChannelId);

  return (
    <main
      className="flex h-full w-full overflow-hidden"
      style={{ fontFamily: "var(--font-body)" }}
    >
      <ServerSidebar />
      <ChannelSidebar />

      {/* Main content */}
      <section
        className="flex-1 flex flex-col min-w-0"
        style={{ background: "var(--color-bg-elevated)" }}
        aria-label="主内容区域"
      >
        <MainHeader channel={channel} />
        <div className="flex-1 flex overflow-hidden">
          <ChatPanel
            channelId={currentChannelId}
            channelName={channel?.name ?? ""}
          />
          <VoiceUsersPanel channelId={currentChannelId} />
        </div>
      </section>
    </main>
  );
}
