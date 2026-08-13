import { useEffect, useRef } from "react";
import { useChatStore } from "@/store/useChatStore";
import MessageItem from "./MessageItem";
import ChatInput from "./ChatInput";

interface ChatPanelProps {
  channelId: string;
  channelName: string;
}

export default function ChatPanel({ channelId, channelName }: ChatPanelProps) {
  const { messages, typingUser, sendMessage, getMessagesByChannel } = useChatStore();
  const scrollRef = useRef<HTMLDivElement>(null);

  const channelMessages = getMessagesByChannel(channelId);

  useEffect(() => {
    if (scrollRef.current) {
      scrollRef.current.scrollTop = scrollRef.current.scrollHeight;
    }
  }, [messages, channelId]);

  return (
    <div className="flex-1 flex flex-col min-w-0">
      <div ref={scrollRef} className="flex-1 overflow-y-auto no-scrollbar px-4">
        {channelMessages.map((msg) => (
          <MessageItem key={msg.id} message={msg} />
        ))}
      </div>

      {/* Typing indicator */}
      <div className="px-4 mb-1" style={{ height: 20 }}>
        {typingUser && (
          <span style={{ fontSize: 11, color: "var(--color-text-tertiary)" }}>
            {typingUser} 正在输入...
          </span>
        )}
      </div>

      <ChatInput
        channelName={channelName}
        onSend={(content) => sendMessage(channelId, "me", content)}
      />
    </div>
  );
}
