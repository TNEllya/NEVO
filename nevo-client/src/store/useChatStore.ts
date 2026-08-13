import { create } from "zustand";
import { messages as initialMessages } from "@/data/mockData";
import type { Message } from "@/types";

interface ChatState {
  messages: Message[];
  typingUser: string | null;
  sendMessage: (channelId: string, userId: string, content: string) => void;
  getMessagesByChannel: (channelId: string) => Message[];
}

export const useChatStore = create<ChatState>((set, get) => ({
  messages: initialMessages,
  typingUser: "张雨涵",
  sendMessage: (channelId, userId, content) => {
    const now = new Date();
    const timestamp = `今天 ${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}`;
    const newMessage: Message = {
      id: `m-${Date.now()}`,
      channelId,
      userId,
      content,
      timestamp,
      type: "user",
    };
    set((state) => ({ messages: [...state.messages, newMessage] }));
  },
  getMessagesByChannel: (channelId) =>
    get().messages.filter((m) => m.channelId === channelId),
}));
