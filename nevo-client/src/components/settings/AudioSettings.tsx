import { useState } from "react";
import { useSettingsStore } from "@/store/useSettingsStore";
import ToggleSwitch from "./ToggleSwitch";
import { ChevronDown, Play, Volume2 } from "lucide-react";

export default function AudioSettings() {
  const s = useSettingsStore();
  const [saved, setSaved] = useState(false);

  const handleSave = () => {
    setSaved(true);
    setTimeout(() => setSaved(false), 2000);
  };

  return (
    <div
      className="flex-1 overflow-y-auto no-scrollbar p-8"
      style={{ background: "var(--color-bg-base)" }}
    >
      {/* 输入设备 */}
      <section className="mb-8">
        <h2
          className="font-semibold mb-4"
          style={{ fontSize: 17, color: "var(--color-text-primary)" }}
        >
          输入设备
        </h2>

        <FieldRow label="麦克风">
          <Select
            value={s.micDevice}
            onChange={s.setMicDevice}
            options={[
              "内置麦克风 (Built-in Microphone)",
              "USB Audio Device",
              "External Microphone",
            ]}
          />
        </FieldRow>

        <FieldRow label="">
          <div style={{ maxWidth: 400 }}>
            <div
              className="overflow-hidden relative rounded-full"
              style={{
                height: 8,
                background: "var(--color-bg-elevated)",
              }}
            >
              <div
                className="rounded-full animate-meter-pulse"
                style={{
                  height: "100%",
                  background:
                    "linear-gradient(90deg, var(--color-primary), var(--color-primary-hover))",
                }}
              />
            </div>
            <div className="flex justify-between mt-1">
              <span style={{ fontSize: 11, color: "var(--color-text-tertiary)" }}>
                音量
              </span>
              <span style={{ fontSize: 11, color: "var(--color-text-tertiary)" }}>
                45%
              </span>
            </div>
          </div>
        </FieldRow>

        <FieldRow label="输入灵敏度">
          <input
            type="range"
            min={0}
            max={100}
            value={s.inputSensitivity}
            onChange={(e) => s.setInputSensitivity(Number(e.target.value))}
            style={{ maxWidth: 400, width: "100%", height: 4 }}
          />
        </FieldRow>

        <FieldRow label="">
          <button
            onClick={handleSave}
            className="inline-flex items-center gap-1.5 px-4 py-1.5 transition-all duration-fast"
            style={{
              background: "var(--color-primary-muted)",
              border: "1px solid rgba(45, 212, 168, 0.25)",
              borderRadius: 4,
              color: "var(--color-primary)",
              fontSize: 13,
              fontFamily: "var(--font-body)",
              whiteSpace: "nowrap",
            }}
            onMouseEnter={(e) => {
              e.currentTarget.style.background = "rgba(45, 212, 168, 0.2)";
              e.currentTarget.style.transform = "translateY(-1px)";
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.background = "var(--color-primary-muted)";
              e.currentTarget.style.transform = "translateY(0)";
            }}
          >
            <Play size={14} />
            测试麦克风
          </button>
        </FieldRow>
      </section>

      <Separator />

      {/* 输出设备 */}
      <section className="mb-8">
        <h2
          className="font-semibold mb-4"
          style={{ fontSize: 17, color: "var(--color-text-primary)" }}
        >
          输出设备
        </h2>

        <FieldRow label="扬声器">
          <Select
            value={s.speakerDevice}
            onChange={s.setSpeakerDevice}
            options={[
              "内置扬声器 (Built-in Speakers)",
              "USB Audio Device",
              "Headphones",
            ]}
          />
        </FieldRow>

        <FieldRow label="输出音量">
          <div className="flex items-center gap-2" style={{ maxWidth: 400, width: "100%" }}>
            <Volume2 size={16} style={{ color: "var(--color-text-tertiary)" }} />
            <input
              type="range"
              min={0}
              max={100}
              value={s.outputVolume}
              onChange={(e) => s.setOutputVolume(Number(e.target.value))}
              style={{ flex: 1, height: 4 }}
            />
            <span
              className="text-right"
              style={{
                fontSize: 13,
                color: "var(--color-text-tertiary)",
                minWidth: 32,
              }}
            >
              {s.outputVolume}%
            </span>
          </div>
        </FieldRow>

        <FieldRow label="">
          <div style={{ maxWidth: 400 }}>
            <div
              className="overflow-hidden rounded-full"
              style={{ height: 8, background: "var(--color-bg-elevated)" }}
            >
              <div
                className="rounded-full"
                style={{
                  width: `${s.outputVolume}%`,
                  height: "100%",
                  background:
                    "linear-gradient(90deg, var(--color-primary), var(--color-primary-hover))",
                }}
              />
            </div>
          </div>
        </FieldRow>
      </section>

      <Separator />

      {/* 高级设置 */}
      <section className="mb-8">
        <h2
          className="font-semibold mb-4"
          style={{ fontSize: 17, color: "var(--color-text-primary)" }}
        >
          高级设置
        </h2>

        <ToggleSwitch
          label="噪声抑制"
          checked={s.noiseSuppression}
          onChange={s.toggleNoiseSuppression}
        />
        <ToggleSwitch
          label="回声消除"
          checked={s.echoCancellation}
          onChange={s.toggleEchoCancellation}
        />
        <ToggleSwitch
          label="自动调节增益"
          checked={s.autoGainControl}
          onChange={s.toggleAutoGainControl}
        />

        <FieldRow label="音频编码">
          <Select
            value={s.audioCodec}
            onChange={s.setAudioCodec}
            options={["Opus 48kHz", "Opus 24kHz", "Opus 16kHz", "Opus 8kHz"]}
          />
        </FieldRow>
      </section>

      <Separator />

      {/* 语音活动检测 */}
      <section className="mb-8">
        <h2
          className="font-semibold mb-4"
          style={{ fontSize: 17, color: "var(--color-text-primary)" }}
        >
          语音活动检测
        </h2>

        <FieldRow label="">
          <div className="flex gap-6">
            <label className="flex items-center gap-2 cursor-pointer">
              <div
                className="flex items-center justify-center rounded-full"
                style={{
                  width: 16,
                  height: 16,
                  border: `2px solid ${
                    s.voiceMode === "voice-activation"
                      ? "var(--color-primary)"
                      : "var(--color-border-hover)"
                  }`,
                }}
                onClick={() => s.setVoiceMode("voice-activation")}
              >
                {s.voiceMode === "voice-activation" && (
                  <div
                    className="rounded-full"
                    style={{
                      width: 8,
                      height: 8,
                      background: "var(--color-primary)",
                    }}
                  />
                )}
              </div>
              <span style={{ fontSize: 13, color: "var(--color-text-primary)" }}>
                语音激活
              </span>
            </label>
            <label className="flex items-center gap-2 cursor-pointer">
              <div
                className="flex items-center justify-center rounded-full transition-colors duration-fast"
                style={{
                  width: 16,
                  height: 16,
                  border: `2px solid ${
                    s.voiceMode === "push-to-talk"
                      ? "var(--color-primary)"
                      : "var(--color-border-hover)"
                  }`,
                }}
                onClick={() => s.setVoiceMode("push-to-talk")}
              >
                {s.voiceMode === "push-to-talk" && (
                  <div
                    className="rounded-full"
                    style={{
                      width: 8,
                      height: 8,
                      background: "var(--color-primary)",
                    }}
                  />
                )}
              </div>
              <span style={{ fontSize: 13, color: "var(--color-text-secondary)" }}>
                按键说话
              </span>
            </label>
          </div>
        </FieldRow>

        <FieldRow label="检测灵敏度">
          <div className="flex items-center gap-2" style={{ maxWidth: 400, width: "100%" }}>
            <input
              type="range"
              min={0}
              max={100}
              value={s.detectionSensitivity}
              onChange={(e) => s.setDetectionSensitivity(Number(e.target.value))}
              style={{ flex: 1, height: 4 }}
            />
            <span
              className="text-right"
              style={{
                fontSize: 13,
                color: "var(--color-text-tertiary)",
                minWidth: 32,
              }}
            >
              {s.detectionSensitivity}%
            </span>
          </div>
        </FieldRow>

        <FieldRow label="按键绑定">
          <div
            className="inline-flex items-center px-3.5 py-1.5 transition-colors duration-fast"
            style={{
              background: "var(--color-bg-elevated)",
              border: "1px solid var(--color-border)",
              borderRadius: 4,
              cursor: "pointer",
            }}
            onMouseEnter={(e) =>
              (e.currentTarget.style.borderColor = "var(--color-border-hover)")
            }
            onMouseLeave={(e) =>
              (e.currentTarget.style.borderColor = "var(--color-border)")
            }
          >
            <span
              style={{
                fontSize: 13,
                color: "var(--color-text-secondary)",
                fontFamily: "var(--font-mono)",
              }}
            >
              {s.pushToTalkKey}
            </span>
          </div>
        </FieldRow>
      </section>

      <Separator />

      {/* Action buttons */}
      <div className="flex items-center gap-4">
        <button
          onClick={handleSave}
          className="inline-flex items-center px-6 py-2.5 transition-all duration-fast"
          style={{
            background: saved ? "var(--color-primary-active)" : "var(--color-primary)",
            border: "none",
            borderRadius: 8,
            color: "var(--color-primary-foreground)",
            fontSize: 13,
            fontWeight: 600,
            fontFamily: "var(--font-body)",
            whiteSpace: "nowrap",
          }}
          onMouseEnter={(e) => {
            e.currentTarget.style.background = "var(--color-primary-hover)";
            e.currentTarget.style.transform = "translateY(-1px)";
          }}
          onMouseLeave={(e) => {
            e.currentTarget.style.background = "var(--color-primary)";
            e.currentTarget.style.transform = "translateY(0)";
          }}
        >
          {saved ? "已保存" : "保存更改"}
        </button>
        <button
          onClick={s.resetDefaults}
          className="inline-flex items-center px-4 py-2.5 transition-colors duration-fast"
          style={{
            background: "transparent",
            border: "none",
            color: "var(--color-text-secondary)",
            fontSize: 13,
            fontFamily: "var(--font-body)",
            whiteSpace: "nowrap",
          }}
          onMouseEnter={(e) =>
            (e.currentTarget.style.color = "var(--color-text-primary)")
          }
          onMouseLeave={(e) =>
            (e.currentTarget.style.color = "var(--color-text-secondary)")
          }
        >
          重置默认
        </button>
      </div>
    </div>
  );
}

function FieldRow({
  label,
  children,
}: {
  label: string;
  children: React.ReactNode;
}) {
  return (
    <div className="flex items-center gap-4 mb-3">
      <label
        className="shrink-0"
        style={{
          width: 140,
          minWidth: 140,
          fontSize: 13,
          color: "var(--color-text-secondary)",
        }}
      >
        {label}
      </label>
      <div className="flex-1" style={{ maxWidth: 400 }}>
        {children}
      </div>
    </div>
  );
}

function Select({
  value,
  onChange,
  options,
}: {
  value: string;
  onChange: (v: string) => void;
  options: string[];
}) {
  return (
    <div className="relative" style={{ maxWidth: 400 }}>
      <select
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className="w-full cursor-pointer outline-none transition-colors duration-fast"
        style={{
          height: 36,
          padding: "0 40px 0 12px",
          background: "var(--color-bg-elevated)",
          border: "1px solid var(--color-border)",
          borderRadius: 4,
          color: "var(--color-text-primary)",
          fontSize: 13,
          fontFamily: "var(--font-body)",
          appearance: "none",
        }}
        onFocus={(e) => (e.currentTarget.style.borderColor = "var(--color-primary)")}
        onBlur={(e) => (e.currentTarget.style.borderColor = "var(--color-border)")}
        onMouseEnter={(e) =>
          (e.currentTarget.style.borderColor = "var(--color-border-hover)")
        }
        onMouseLeave={(e) =>
          (e.currentTarget.style.borderColor = "var(--color-border)")
        }
      >
        {options.map((opt) => (
          <option key={opt} value={opt}>
            {opt}
          </option>
        ))}
      </select>
      <ChevronDown
        size={14}
        style={{
          position: "absolute",
          right: 12,
          top: "50%",
          transform: "translateY(-50%)",
          color: "var(--color-text-tertiary)",
          pointerEvents: "none",
        }}
      />
    </div>
  );
}

function Separator() {
  return (
    <div
      className="mb-8"
      style={{ height: 1, background: "var(--color-border)" }}
    />
  );
}
