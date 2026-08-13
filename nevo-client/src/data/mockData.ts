import type { Server, Channel, User, Message, VoiceMember } from "@/types";

export const servers: Server[] = [
  { id: "nhq", name: "NEVO 总部", abbr: "NHQ", color: "#2DD4A8" },
  { id: "dev", name: "开发组", abbr: "DEV", color: "#8B5CF6" },
  { id: "gm", name: "游戏组", abbr: "GM", color: "#F59E0B" },
  { id: "m", name: "市场组", abbr: "M", color: "#EC4899" },
];

export const channels: Channel[] = [
  // NEVO 总部 文字频道
  { id: "general", serverId: "nhq", name: "综合", type: "text", category: "文字频道", topic: "欢迎来到综合频道，请保持友善交流" },
  { id: "tech", serverId: "nhq", name: "技术讨论", type: "text", category: "文字频道", topic: "技术相关讨论" },
  { id: "announce", serverId: "nhq", name: "公告", type: "text", category: "文字频道", unread: 3, topic: "重要公告发布" },
  // NEVO 总部 语音频道
  { id: "voice-lobby", serverId: "nhq", name: "语音大厅", type: "voice", category: "语音频道", topic: "欢迎来到语音大厅，请保持友善交流" },
  { id: "voice-team", serverId: "nhq", name: "小组讨论", type: "voice", category: "语音频道" },
  { id: "voice-rest", serverId: "nhq", name: "休息室", type: "voice", category: "语音频道" },
];

export const users: User[] = [
  { id: "me", name: "我", abbr: "我", avatarColor: "#2DD4A8", status: "online" },
  { id: "lin", name: "林浩宇", abbr: "林", avatarColor: "#3B82F6", status: "speaking" },
  { id: "zhang", name: "张雨涵", abbr: "张", avatarColor: "#8B5CF6", status: "speaking" },
  { id: "wang", name: "王晓明", abbr: "王", avatarColor: "#EC4899", status: "muted" },
  { id: "chen", name: "陈思远", abbr: "陈", avatarColor: "#F59E0B", status: "idle" },
  { id: "liu", name: "刘佳怡", abbr: "刘", avatarColor: "#10B981", status: "idle" },
];

export const messages: Message[] = [
  { id: "m1", channelId: "voice-lobby", userId: "system", content: "陈思远 加入了语音频道", timestamp: "今天 14:31", type: "system" },
  { id: "m2", channelId: "voice-lobby", userId: "lin", content: "大家好，今天的技术分享会准备得怎么样了？", timestamp: "今天 14:32", type: "user" },
  { id: "m3", channelId: "voice-lobby", userId: "zhang", content: "PPT 已经做好了，主要是关于新版本 WebRTC 优化的部分。大家可以先看看。", timestamp: "今天 14:33", type: "user" },
  { id: "m4", channelId: "voice-lobby", userId: "chen", content: "好的，期待。我这边编解码模块测试结果也出来了，延迟降低了约 15%。", timestamp: "今天 14:35", type: "user" },
  { id: "m5", channelId: "voice-lobby", userId: "liu", content: "太好了，我正在整理测试报告，一会儿共享屏幕给大家看。", timestamp: "今天 14:38", type: "user" },
  { id: "m6", channelId: "voice-lobby", userId: "system", content: "刘佳怡 开始屏幕共享", timestamp: "今天 14:39", type: "system" },
  { id: "m7", channelId: "voice-lobby", userId: "lin", content: "可以开始了，大家都准备好了吗？", timestamp: "今天 14:40", type: "user" },
  // 综合频道消息
  { id: "m8", channelId: "general", userId: "lin", content: "早上好，今天也要加油！", timestamp: "今天 09:00", type: "user" },
  { id: "m9", channelId: "general", userId: "zhang", content: "新版本已经发布，欢迎反馈问题。", timestamp: "今天 10:15", type: "user" },
];

export const voiceMembers: VoiceMember[] = [
  { id: "vm1", channelId: "voice-lobby", userId: "lin", voiceState: "speaking", micOn: true },
  { id: "vm2", channelId: "voice-lobby", userId: "zhang", voiceState: "speaking", micOn: true },
  { id: "vm3", channelId: "voice-lobby", userId: "wang", voiceState: "muted", micOn: false },
  { id: "vm4", channelId: "voice-lobby", userId: "chen", voiceState: "idle", micOn: true },
  { id: "vm5", channelId: "voice-lobby", userId: "liu", voiceState: "idle", micOn: true },
];

export const defaultAudioSettings = {
  micDevice: "内置麦克风 (Built-in Microphone)",
  inputSensitivity: 65,
  speakerDevice: "内置扬声器 (Built-in Speakers)",
  outputVolume: 75,
  noiseSuppression: true,
  echoCancellation: true,
  autoGainControl: false,
  audioCodec: "Opus 48kHz",
  voiceMode: "voice-activation" as const,
  detectionSensitivity: 50,
  pushToTalkKey: "鼠标中键",
};

export function getUser(userId: string): User | undefined {
  if (userId === "system") {
    return { id: "system", name: "系统", abbr: "S", avatarColor: "#6B7280", status: "online" };
  }
  return users.find((u) => u.id === userId);
}

export function getChannelsByServer(serverId: string): Channel[] {
  return channels.filter((c) => c.serverId === serverId);
}

export function getMessagesByChannel(channelId: string): Message[] {
  return messages.filter((m) => m.channelId === channelId);
}

export function getVoiceMembersByChannel(channelId: string): VoiceMember[] {
  return voiceMembers.filter((vm) => vm.channelId === channelId);
}
