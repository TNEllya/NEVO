import { useNavigate } from "react-router-dom";
import { useAppStore } from "@/store/useAppStore";
import { useVoiceStore } from "@/store/useVoiceStore";
import Avatar from "@/components/shared/Avatar";
import VoiceBars from "./VoiceBars";
import { Mic, MicOff, Video } from "lucide-react";
import type { VoiceMember } from "@/types";

interface VoiceUserItemProps {
  member: VoiceMember;
}

export default function VoiceUserItem({ member }: VoiceUserItemProps) {
  const navigate = useNavigate();
  const { getUser } = useAppStore();
  const { toggleMemberMute } = useVoiceStore();
  const user = getUser(member.userId);
  const isSpeaking = member.voiceState === "speaking";

  const handleClick = () => {
    if (member.userId !== "me") {
      navigate(`/video-call/${member.userId}`);
    }
  };

  return (
    <div
      className="flex items-center gap-3 px-3 py-2.5 rounded-md cursor-pointer transition-colors duration-fast"
      style={{
        background: isSpeaking ? "var(--color-primary-muted)" : "transparent",
      }}
      onMouseEnter={(e) => {
        if (!isSpeaking)
          e.currentTarget.style.background = "var(--color-bg-hover)";
      }}
      onMouseLeave={(e) => {
        if (!isSpeaking) e.currentTarget.style.background = "transparent";
      }}
      onClick={handleClick}
      title={member.userId !== "me" ? `与 ${user?.name} 发起视频通话` : undefined}
    >
      <div className="relative" style={{ width: 36, height: 36, minWidth: 36 }}>
        <Avatar user={user} size={36} />
        {isSpeaking && (
          <div
            className="absolute rounded-full"
            style={{
              inset: -3,
              border: "2px solid var(--color-primary)",
              opacity: 0.6,
            }}
          />
        )}
        {member.voiceState === "muted" && (
          <div
            className="absolute flex items-center justify-center rounded-full border-2"
            style={{
              bottom: -2,
              right: -2,
              width: 16,
              height: 16,
              background: "var(--state-error)",
              borderColor: "var(--color-bg-surface)",
            }}
          >
            <MicOff size={10} className="text-white" />
          </div>
        )}
      </div>

      <div className="flex-1 min-w-0">
        <div className="flex items-center gap-1.5">
          <span
            className="font-medium truncate"
            style={{
              fontSize: 14,
              color:
                member.voiceState === "muted"
                  ? "var(--color-text-tertiary)"
                  : isSpeaking
                  ? "var(--color-text-primary)"
                  : "var(--color-text-secondary)",
            }}
          >
            {user?.name}
          </span>
          {isSpeaking && (
            <div
              className="rounded-full"
              style={{
                width: 6,
                height: 6,
                minWidth: 6,
                background: "var(--color-primary)",
              }}
            />
          )}
        </div>
        <VoiceBars state={member.voiceState} />
      </div>

      <button
        onClick={(e) => {
          e.stopPropagation();
          toggleMemberMute(member.id);
        }}
        className="p-0.5 rounded-sm transition-colors duration-fast"
        style={{
          color: member.micOn
            ? "var(--color-text-tertiary)"
            : "var(--state-error)",
        }}
        aria-label={member.micOn ? "静音" : "取消静音"}
      >
        {member.micOn ? <Mic size={16} /> : <MicOff size={16} />}
      </button>

      {member.userId !== "me" && (
        <button
          onClick={(e) => {
            e.stopPropagation();
            navigate(`/video-call/${member.userId}`);
          }}
          className="p-0.5 rounded-sm transition-colors duration-fast opacity-0 hover:opacity-100"
          style={{ color: "var(--color-primary)" }}
          aria-label="视频通话"
        >
          <Video size={16} />
        </button>
      )}
    </div>
  );
}
