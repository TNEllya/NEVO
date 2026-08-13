import { useAppStore } from "@/store/useAppStore";
import Avatar from "@/components/shared/Avatar";
import type { Message } from "@/types";

export default function MessageItem({ message }: { message: Message }) {
  const { getUser } = useAppStore();

  if (message.type === "system") {
    return (
      <div className="py-2">
        <span
          className="italic"
          style={{ fontSize: 13, color: "var(--color-text-tertiary)" }}
        >
          {message.content}
        </span>
      </div>
    );
  }

  const user = getUser(message.userId);

  return (
    <div className="flex gap-3 py-2">
      <Avatar user={user} size={40} />
      <div className="flex-1 min-w-0">
        <div className="flex items-baseline gap-2 mb-0.5">
          <span
            className="font-semibold cursor-pointer"
            style={{ fontSize: 14, color: "var(--color-primary)" }}
          >
            {user?.name}
          </span>
          <span style={{ fontSize: 11, color: "var(--color-text-tertiary)" }}>
            {message.timestamp}
          </span>
        </div>
        <div
          style={{
            fontSize: 14,
            color: "var(--color-text-primary)",
            lineHeight: 1.5,
          }}
        >
          {message.content}
        </div>
      </div>
    </div>
  );
}
