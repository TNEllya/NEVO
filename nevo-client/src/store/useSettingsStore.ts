import { create } from "zustand";
import { persist } from "zustand/middleware";
import { defaultAudioSettings } from "@/data/mockData";
import type { AudioSettings } from "@/types";

interface SettingsState extends AudioSettings {
  setMicDevice: (v: string) => void;
  setInputSensitivity: (v: number) => void;
  setSpeakerDevice: (v: string) => void;
  setOutputVolume: (v: number) => void;
  toggleNoiseSuppression: () => void;
  toggleEchoCancellation: () => void;
  toggleAutoGainControl: () => void;
  setAudioCodec: (v: string) => void;
  setVoiceMode: (v: "voice-activation" | "push-to-talk") => void;
  setDetectionSensitivity: (v: number) => void;
  resetDefaults: () => void;
}

export const useSettingsStore = create<SettingsState>()(
  persist(
    (set) => ({
      ...defaultAudioSettings,
      setMicDevice: (v) => set({ micDevice: v }),
      setInputSensitivity: (v) => set({ inputSensitivity: v }),
      setSpeakerDevice: (v) => set({ speakerDevice: v }),
      setOutputVolume: (v) => set({ outputVolume: v }),
      toggleNoiseSuppression: () => set((s) => ({ noiseSuppression: !s.noiseSuppression })),
      toggleEchoCancellation: () => set((s) => ({ echoCancellation: !s.echoCancellation })),
      toggleAutoGainControl: () => set((s) => ({ autoGainControl: !s.autoGainControl })),
      setAudioCodec: (v) => set({ audioCodec: v }),
      setVoiceMode: (v) => set({ voiceMode: v }),
      setDetectionSensitivity: (v) => set({ detectionSensitivity: v }),
      resetDefaults: () => set({ ...defaultAudioSettings }),
    }),
    { name: "nevo-settings" }
  )
);
