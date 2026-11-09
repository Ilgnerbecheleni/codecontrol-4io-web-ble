export interface BoardState {
  deviceId: string;
  version: string;
  outputs: boolean[];
  inputs: boolean[];
  outputNames: string[];
  inputNames: string[];
  physicalOutputsEnabled: boolean;
  wifi: { connected: boolean; ssid: string; ip: string; rssi: number };
  mqtt: { connected: boolean; host: string; port: number; tls: boolean; user: string; topicPrefix: string };
}
export interface BoardReply {
  id?: number;
  ok: boolean;
  error?: string;
  deviceId?: string;
  nonce?: string;
  version?: string;
  state?: BoardState;
  message?: string;
}
export type ConnectionState = 'disconnected' | 'connecting' | 'connected' | 'authenticated';
