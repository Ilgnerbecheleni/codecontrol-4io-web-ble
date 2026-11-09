import type { BoardReply, ConnectionState } from './types';

export const SERVICE_UUID = '8a12f100-587c-4c9f-9603-2f83c1a64001';
export const WRITE_UUID =   '8a12f101-587c-4c9f-9603-2f83c1a64001';
export const NOTIFY_UUID =  '8a12f102-587c-4c9f-9603-2f83c1a64001';
const encoder = new TextEncoder();

// lib.dom em algumas versoes de TypeScript nao traz Web Bluetooth.
interface BleDevice extends EventTarget {
  name?: string;
  gatt?: { connected: boolean; connect(): Promise<BleServer>; disconnect(): void };
}
interface BleServer { getPrimaryService(uuid: string): Promise<BleService> }
interface BleService { getCharacteristic(uuid: string): Promise<BleCharacteristic> }
interface BleCharacteristic extends EventTarget {
  value?: DataView;
  startNotifications(): Promise<BleCharacteristic>;
  writeValueWithResponse(value: BufferSource): Promise<void>;
}
interface BleNavigator { bluetooth?: { requestDevice(options: { filters: Array<{ services: string[] }>; optionalServices: string[] }): Promise<BleDevice> } }
type Pending = { resolve: (reply: BoardReply) => void; reject: (err: Error) => void; timer: number };

export class BoardBle {
  onStatus: (status: ConnectionState) => void = () => {};
  onLog: (line: string) => void = () => {};
  private device: BleDevice | null = null;
  private writeCharacteristic: BleCharacteristic | null = null;
  private notifyCharacteristic: BleCharacteristic | null = null;
  private decoder = new TextDecoder();
  private rxBuffer = '';
  private pending = new Map<number, Pending>();
  private id = 1;
  private tail: Promise<void> = Promise.resolve();
  state: ConnectionState = 'disconnected';
  deviceId = '';
  nonce = '';

  supported(): boolean { return !!(navigator as Navigator & BleNavigator).bluetooth && window.isSecureContext; }
  private update(state: ConnectionState) { this.state = state; this.onStatus(state); }

  async connect(): Promise<void> {
    if (!this.supported()) throw new Error('Web Bluetooth não está disponível. Use Chrome no Android ou Chrome/Edge no computador com HTTPS.');
    this.disconnect();
    this.update('connecting');
    try {
      const bt = (navigator as Navigator & BleNavigator).bluetooth!;
      this.device = await bt.requestDevice({ filters: [{ services: [SERVICE_UUID] }], optionalServices: [SERVICE_UUID] });
      this.device.addEventListener('gattserverdisconnected', this.handleDisconnected);
      if (!this.device.gatt) throw new Error('O dispositivo não oferece GATT.');
      const server = await this.device.gatt.connect();
      const service = await server.getPrimaryService(SERVICE_UUID);
      this.writeCharacteristic = await service.getCharacteristic(WRITE_UUID);
      this.notifyCharacteristic = await service.getCharacteristic(NOTIFY_UUID);
      this.notifyCharacteristic.addEventListener('characteristicvaluechanged', this.handleNotification);
      await this.notifyCharacteristic.startNotifications();
      this.update('connected');
      const hello = await this.request({ cmd: 'hello' });
      if (!hello.ok || !hello.deviceId || !hello.nonce) throw new Error('Handshake BLE inválido.');
      this.deviceId = hello.deviceId;
      this.nonce = hello.nonce;
      this.onLog(`Placa ${this.deviceId} localizada. Aguardando autenticação.`);
    } catch (e) { this.disconnect(); throw e; }
  }

  async authenticate(secret: string): Promise<void> {
    if (!this.deviceId || !this.nonce) throw new Error('Conecte-se primeiro.');
    if (secret.trim().length < 32) throw new Error('A chave de provisionamento precisa ter pelo menos 32 caracteres.');
    const key = await crypto.subtle.importKey('raw', encoder.encode(secret.trim()), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
    const proof = await crypto.subtle.sign('HMAC', key, encoder.encode(`${this.deviceId}:${this.nonce}`));
    const hex = [...new Uint8Array(proof)].map(byte => byte.toString(16).padStart(2, '0')).join('');
    const reply = await this.request({ cmd: 'auth', proof: hex });
    if (!reply.ok) throw new Error(reply.error || 'Autenticação recusada.');
    this.update('authenticated');
    this.onLog('Autenticado com a placa.');
  }

  async request(body: Record<string, unknown>): Promise<BoardReply> {
    // Fila serial: nao mistura respostas e escritas BLE de comandos distintos.
    const operation = this.tail.catch(() => {}).then(() => this.send(body));
    this.tail = operation.then(() => {}, () => {});
    return operation;
  }

  private async send(body: Record<string, unknown>): Promise<BoardReply> {
    if (!this.writeCharacteristic) throw new Error('Bluetooth desconectado.');
    const id = this.id++;
    const bytes = encoder.encode(JSON.stringify({ ...body, id }) + '\n');
    if (bytes.length > 4096) throw new Error('Configuração BLE excede 4096 bytes.');
    let done: Pending | undefined;
    const reply = new Promise<BoardReply>((resolve, reject) => {
      const timer = window.setTimeout(() => { this.pending.delete(id); reject(new Error('Tempo de resposta esgotado. Reconecte e tente novamente.')); }, 30000);
      done = { resolve, reject, timer };
      this.pending.set(id, done);
    });
    try {
      // 20 bytes funcionam mesmo quando o ATT_MTU ainda esta no tamanho minimo.
      for (let offset = 0; offset < bytes.length; offset += 20) {
        await this.writeCharacteristic.writeValueWithResponse(bytes.slice(offset, offset + 20));
      }
    } catch (error) {
      if (done) { window.clearTimeout(done.timer); this.pending.delete(id); }
      throw error;
    }
    return reply;
  }

  private handleNotification = (event: Event) => {
    const characteristic = event.target as BleCharacteristic;
    const value = characteristic.value;
    if (!value) return;
    const chunk = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    this.rxBuffer += this.decoder.decode(chunk, { stream: true });
    if (this.rxBuffer.length > 12000) { this.rxBuffer = ''; this.onLog('Mensagem BLE de resposta muito longa.'); return; }
    let idx = -1;
    while ((idx = this.rxBuffer.indexOf('\n')) >= 0) {
      const line = this.rxBuffer.slice(0, idx);
      this.rxBuffer = this.rxBuffer.slice(idx + 1);
      try {
        const message = JSON.parse(line) as BoardReply;
        const pending = this.pending.get(message.id ?? -1);
        if (pending) {
          window.clearTimeout(pending.timer);
          this.pending.delete(message.id!);
          pending.resolve(message);
        } else {
          this.onLog(`Resposta não solicitada: ${line.slice(0, 130)}`);
        }
      } catch { this.onLog('Resposta JSON inválida.'); }
    }
  };
  private handleDisconnected = () => {
    for (const [id, p] of this.pending) { window.clearTimeout(p.timer); p.reject(new Error('ESP32 desconectado.')); this.pending.delete(id); }
    this.writeCharacteristic = null;
    this.notifyCharacteristic = null;
    this.deviceId = '';
    this.nonce = '';
    this.rxBuffer = '';
    this.decoder = new TextDecoder();
    this.update('disconnected');
  };
  disconnect() {
    if (this.notifyCharacteristic) this.notifyCharacteristic.removeEventListener('characteristicvaluechanged', this.handleNotification);
    if (this.device) {
      this.device.removeEventListener('gattserverdisconnected', this.handleDisconnected);
      if (this.device.gatt?.connected) this.device.gatt.disconnect();
    }
    this.device = null;
    this.handleDisconnected();
  }
}
