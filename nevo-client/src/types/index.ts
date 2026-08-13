export type ChannelType = "text" | "voice";
export type UserStatus = "online" | "idle" | "muted" | "speaking";
export type VoiceState = "speaking" | "muted" | "idle";
export type MessageType = "user" | "system";

export interface Server {
  id: string;
  name: string;
  abbr: string;
  color: string;
}

export interface Channel {
  id: string;
  serverId: string;
  name: string;
  type: ChannelType;
  category: string;
  unread?: number;
  topic?: string;
}

export interface User {
  id: string;
  name: string;
  abbr: string;
  avatarColor: string;
  status: UserStatus;
}

export interface Message {
  id: string;
  channelId: string;
  userId: string;
  content: string;
  timestamp: string;
  type: MessageType;
}

export interface VoiceMember {
  id: string;
  channelId: string;
  userId: string;
  voiceState: VoiceState;
  micOn: boolean;
}

export type SettingsSection =
  | "account"
  | "audio"
  | "video"
  | "notifications"
  | "hotkeys"
  | "about";

export interface AudioSettings {
  micDevice: string;
  inputSensitivity: number;
  speakerDevice: string;
  outputVolume: number;
  noiseSuppression: boolean;
  echoCancellation: boolean;
  autoGainControl: boolean;
  audioCodec: string;
  voiceMode: "voice-activation" | "push-to-talk";
  detectionSensitivity: number;
  pushToTalkKey: string;
}
