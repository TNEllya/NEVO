import { create } from "zustand";
import { voiceMembers as initialMembers } from "@/data/mockData";
import type { VoiceMember } from "@/types";

interface VoiceState {
  voiceMembers: VoiceMember[];
  connected: boolean;
  latency: number;
  selfMuted: boolean;
  deafened: boolean;
  toggleSelfMute: () => void;
  toggleDeafen: () => void;
  getVoiceMembersByChannel: (channelId: string) => VoiceMember[];
  toggleMemberMute: (memberId: string) => void;
}

export const useVoiceStore = create<VoiceState>((set, get) => ({
  voiceMembers: initialMembers,
  connected: true,
  latency: 12,
  selfMuted: false,
  deafened: false,
  toggleSelfMute: () => set((s) => ({ selfMuted: !s.selfMuted })),
  toggleDeafen: () => set((s) => ({ deafened: !s.deafened, selfMuted: s.deafened ? s.selfMuted : true })),
  getVoiceMembersByChannel: (channelId) =>
    get().voiceMembers.filter((vm) => vm.channelId === channelId),
  toggleMemberMute: (memberId) =>
    set((s) => ({
      voiceMembers: s.voiceMembers.map((vm) =>
        vm.id === memberId ? { ...vm, micOn: !vm.micOn, voiceState: !vm.micOn ? "speaking" : "muted" } : vm
      ),
    })),
}));
