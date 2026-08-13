import type { VoiceState } from "@/types";

interface VoiceBarsProps {
  state: VoiceState;
}

const SPEAK_DELAYS = ["0s", "0.1s", "0.05s", "0.15s", "0.08s"];
const SPEAK_DURATIONS = ["0.4s", "0.35s", "0.45s", "0.38s", "0.42s"];
const IDLE_DELAYS = ["0s", "0.2s", "0.1s", "0.3s", "0.15s"];
const IDLE_DURATIONS = ["1.2s", "1s", "1.4s", "1.1s", "1.3s"];

export default function VoiceBars({ state }: VoiceBarsProps) {
  if (state === "muted") {
    return (
      <div className="flex items-center gap-[2px] mt-1">
        {[0, 1, 2, 3, 4].map((i) => (
          <div
            key={i}
            className="w-[3px] h-1 rounded-sm"
            style={{ background: "var(--color-voice-idle)" }}
          />
        ))}
      </div>
    );
  }

  if (state === "speaking") {
    return (
      <div className="flex items-center gap-[2px] mt-1">
        {SPEAK_DURATIONS.map((dur, i) => (
          <div
            key={i}
            className="w-[3px] rounded-sm animate-voice-bar"
            style={{
              animationDuration: dur,
              animationDelay: SPEAK_DELAYS[i],
              background: "var(--color-voice-active)",
              boxShadow: "0 0 4px var(--color-voice-active)",
            }}
          />
        ))}
      </div>
    );
  }

  // idle
  return (
    <div className="flex items-center gap-[2px] mt-1">
      {IDLE_DURATIONS.map((dur, i) => (
        <div
          key={i}
          className="w-[3px] rounded-sm animate-voice-bar-idle"
          style={{
            animationDuration: dur,
            animationDelay: IDLE_DELAYS[i],
            background: "var(--color-voice-idle)",
          }}
        />
      ))}
    </div>
  );
}
