import { cn } from "@/lib/utils";
import type { User } from "@/types";

interface AvatarProps {
  user?: User;
  size?: number;
  showStatus?: boolean;
  statusColor?: string;
  className?: string;
}

export default function Avatar({
  user,
  size = 40,
  showStatus = false,
  statusColor,
  className,
}: AvatarProps) {
  const fontSize = Math.max(11, Math.floor(size * 0.4));
  return (
    <div
      className={cn("relative shrink-0", className)}
      style={{ width: size, height: size }}
    >
      <div
        className="flex h-full w-full items-center justify-center rounded-full font-semibold text-white"
        style={{ background: user?.avatarColor ?? "#6B7280" }}
      >
        <span style={{ fontSize }}>{user?.abbr ?? "?"}</span>
      </div>
      {showStatus && (
        <div
          className="absolute rounded-full border-2"
          style={{
            bottom: -1,
            right: -1,
            width: Math.max(10, size * 0.28),
            height: Math.max(10, size * 0.28),
            background: statusColor ?? "var(--color-primary)",
            borderColor: "var(--color-bg-surface)",
          }}
        />
      )}
    </div>
  );
}
