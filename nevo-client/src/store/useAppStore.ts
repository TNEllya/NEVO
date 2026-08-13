import { create } from "zustand";
import { servers, channels, users } from "@/data/mockData";
import type { Server, Channel, User } from "@/types";

interface AppState {
  servers: Server[];
  channels: Channel[];
  users: User[];
  currentServerId: string;
  currentChannelId: string;
  setServer: (serverId: string) => void;
  setChannel: (channelId: string) => void;
  getServer: (id: string) => Server | undefined;
  getChannel: (id: string) => Channel | undefined;
  getUser: (id: string) => User | undefined;
  getChannelsByServer: (serverId: string) => Channel[];
}

export const useAppStore = create<AppState>((set, get) => ({
  servers,
  channels,
  users,
  currentServerId: "nhq",
  currentChannelId: "voice-lobby",
  setServer: (serverId) => {
    const firstChannel = get().channels.find((c) => c.serverId === serverId);
    set({ currentServerId: serverId, currentChannelId: firstChannel?.id ?? "" });
  },
  setChannel: (channelId) => set({ currentChannelId: channelId }),
  getServer: (id) => get().servers.find((s) => s.id === id),
  getChannel: (id) => get().channels.find((c) => c.id === id),
  getUser: (id) => {
    if (id === "system") {
      return { id: "system", name: "系统", abbr: "S", avatarColor: "#6B7280", status: "online" as const };
    }
    return get().users.find((u) => u.id === id);
  },
  getChannelsByServer: (serverId) => get().channels.filter((c) => c.serverId === serverId),
}));
