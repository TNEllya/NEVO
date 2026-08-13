import { useState } from "react";
import { Smile, Plus, SendHorizontal } from "lucide-react";

interface ChatInputProps {
  channelName: string;
  onSend: (content: string) => void;
}

export default function ChatInput({ channelName, onSend }: ChatInputProps) {
  const [value, setValue] = useState("");

  const handleSend = () => {
    if (!value.trim()) return;
    onSend(value.trim());
    setValue("");
  };

  return (
    <div className="px-4 pb-4">
      <div
        className="flex items-center gap-1 px-2 py-1"
        style={{
          background: "var(--color-bg-surface)",
          border: "1px solid var(--color-border)",
          borderRadius: 8,
          minHeight: 44,
        }}
      >
        <button
          className="p-1 rounded-sm transition-opacity duration-fast"
          aria-label="表情"
        >
          <Smile
            size={20}
            style={{ color: "var(--color-text-tertiary)" }}
          />
        </button>

        <input
          type="text"
          placeholder={`在 #${channelName} 发送消息`}
          value={value}
          onChange={(e) => setValue(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter") handleSend();
          }}
          className="flex-1 bg-transparent outline-none px-1 py-1.5"
          style={{
            color: "var(--color-text-primary)",
            fontSize: 14,
            fontFamily: "var(--font-body)",
          }}
          aria-label="消息输入框"
        />

        <button
          className="p-1 rounded-sm transition-opacity duration-fast"
          aria-label="附件"
        >
          <Plus
            size={20}
            style={{ color: "var(--color-text-tertiary)" }}
          />
        </button>

        <button
          onClick={handleSend}
          className="p-1 rounded-sm transition-colors duration-fast"
          onMouseEnter={(e) =>
            (e.currentTarget.style.background = "var(--color-bg-hover)")
          }
          onMouseLeave={(e) =>
            (e.currentTarget.style.background = "transparent")
          }
          aria-label="发送"
        >
          <SendHorizontal
            size={20}
            style={{ color: "var(--color-primary)", fill: "var(--color-primary)" }}
          />
        </button>
      </div>
    </div>
  );
}
