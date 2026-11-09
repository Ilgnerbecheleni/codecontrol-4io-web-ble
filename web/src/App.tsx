import { useEffect, useRef, useState } from 'react';
import { Activity, Bluetooth, BluetoothOff, Cable, CheckCircle2, ChevronRight, CircleAlert, Cloud, Cpu, LockKeyhole, Power, Radio, RefreshCcw, RotateCcw, Save, Settings2, ShieldCheck, Signal, SlidersHorizontal, Wifi, WifiOff } from 'lucide-react';
import { BoardBle } from './ble';
import type { BoardReply, BoardState, ConnectionState } from './types';

type Tab = 'panel' | 'labels' | 'network' | 'mqtt' | 'service';
const tabs: { id: Tab; title: string; icon: typeof Cpu }[] = [
  { id: 'panel', title: 'Painel', icon: SlidersHorizontal },
  { id: 'labels', title: 'Canais', icon: Cable },
  { id: 'network', title: 'Wi-Fi', icon: Wifi },
  { id: 'mqtt', title: 'MQTT', icon: Cloud },
  { id: 'service', title: 'Sistema', icon: Settings2 },
];

export default function App() {
  const ble = useRef(new BoardBle());
  const [connection, setConnection] = useState<ConnectionState>('disconnected');
  const [board, setBoard] = useState<BoardState | null>(null);
  const [tab, setTab] = useState<Tab>('panel');
  const [secret, setSecret] = useState('');
  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState<{ text: string; type: 'ok' | 'error' } | null>(null);
  const [logs, setLogs] = useState<string[]>([]);
  const [outNames, setOutNames] = useState<string[]>(['Saída 1', 'Saída 2', 'Saída 3', 'Saída 4']);
  const [inNames, setInNames] = useState<string[]>(['Entrada 1', 'Entrada 2', 'Entrada 3', 'Entrada 4']);
  const [ssid, setSsid] = useState('');
  const [wifiPass, setWifiPass] = useState('');
  const [mqtt, setMqtt] = useState({ host: '', port: 8883, tls: true, user: '', password: '', topicPrefix: '', caPem: '' });
  const [resetChoice, setResetChoice] = useState<'wifi' | 'factory'>('wifi');
  const authenticated = connection === 'authenticated';

  useEffect(() => {
    const client = ble.current;
    client.onStatus = (s) => { setConnection(s); if (s === 'disconnected') { setBoard(null); setSecret(''); } };
    client.onLog = (message) => setLogs(prev => [`${new Date().toLocaleTimeString('pt-BR')} · ${message}`, ...prev].slice(0, 20));
    return () => client.disconnect();
  }, []);
  // Atualiza o painel a cada 3 segundos quando existe uma sessao BLE autenticada.
  // Usa apenas setBoard para nao sobrescrever rascunhos nas abas de configuracao.
  useEffect(() => {
    if (connection !== 'authenticated' || tab !== 'panel' || busy) return;
    const timer = window.setInterval(async () => {
      try {
        const response = await ble.current.request({ cmd: 'state' });
        if (response.ok && response.state) setBoard(response.state);
      } catch { /* Erros de BLE sao mostrados na proxima operacao manual. */ }
    }, 3000);
    return () => window.clearInterval(timer);
  }, [connection, tab, busy]);
  const info = (text: string, type: 'ok' | 'error' = 'ok') => setNotice({ text, type });
  const run = async (task: () => Promise<void>) => {
    setBusy(true); setNotice(null);
    try { await task(); } catch (e) { info(e instanceof Error ? e.message : 'Erro desconhecido.', 'error'); }
    finally { setBusy(false); }
  };
  const syncForm = (state: BoardState) => {
    setBoard(state);
    setOutNames(state.outputNames);
    setInNames(state.inputNames);
    setSsid(state.wifi.ssid);
    setMqtt(old => ({ ...old, host: state.mqtt.host, port: state.mqtt.port || 8883, tls: state.mqtt.tls, user: state.mqtt.user, topicPrefix: state.mqtt.topicPrefix, password: '', caPem: '' }));
  };
  const ensureOk = (reply: BoardReply) => { if (!reply.ok) throw new Error(reply.error || 'A placa recusou o comando.'); return reply; };
  const refresh = async () => {
    const r = ensureOk(await ble.current.request({ cmd: 'state' }));
    if (!r.state) throw new Error('Firmware não retornou o estado.');
    syncForm(r.state);
  };
  const connect = () => run(async () => { await ble.current.connect(); info('Placa encontrada. Insira a chave individual de provisionamento.'); });
  const auth = () => run(async () => { await ble.current.authenticate(secret); setSecret(''); await refresh(); info('Autenticado e sincronizado.'); });
  const send = (command: Record<string, unknown>, success: string, reload = true) => run(async () => {
    ensureOk(await ble.current.request(command));
    if (reload) await refresh();
    info(success);
  });
  const changeOutput = (i: number, value: boolean) => send({ cmd: 'set_output', channel: i + 1, value }, `Saída ${i + 1} atualizada.`);
  const changeName = (type: 'out' | 'in', i: number, value: string) => {
    const setter = type === 'out' ? setOutNames : setInNames;
    setter(old => old.map((s, idx) => idx === i ? value : s));
  };
  const supported = ble.current.supported();

  return <div className="app-shell">
    <aside className="sidebar">
      <div className="brand"><div className="brand-mark"><Cpu size={23}/></div><div><strong>CodeControl</strong><small>SMART I/O · CONFIGURADOR</small></div></div>
      <p className="side-heading">CONFIGURAÇÃO DA PLACA</p>
      <nav>{tabs.map(t => <button className={`nav-link ${tab === t.id ? 'active' : ''}`} key={t.id} onClick={() => setTab(t.id)}><t.icon size={18}/><span>{t.title}</span><ChevronRight size={16} className="chevron"/></button>)}</nav>
      <div className="side-bottom"><ShieldCheck size={18}/><span>Conexão local via BLE<br/><small>Sem enviar credenciais à nuvem</small></span></div>
    </aside>
    <main className="content">
      <header className="topbar"><div className="top-title"><div className="eyebrow">CONFIGURADOR DE DISPOSITIVOS</div><h1>ESP32 · 4IO</h1><p>Controle e configure sua placa pelo navegador</p></div><div className={`connection-pill ${authenticated ? 'connected' : ''}`}><span className="status-led"/>{authenticated ? 'Autenticado' : connection === 'connected' ? 'Aguardando chave' : connection === 'connecting' ? 'Conectando...' : 'Desconectado'}</div></header>
      {!supported && <div className="callout danger"><CircleAlert size={19}/><span>Web Bluetooth indisponível. Use Chrome no Android ou Chrome/Edge no Windows, em HTTPS (GitHub Pages) ou localhost.</span></div>}
      {notice && <div role="status" className={`callout ${notice.type === 'error' ? 'danger' : 'success'}`}><span>{notice.type === 'error' ? <CircleAlert size={19}/> : <CheckCircle2 size={19}/>}</span><span>{notice.text}</span><button className="dismiss" onClick={() => setNotice(null)}>×</button></div>}
      <section className="device-banner"><div className="device-icon"><Cpu size={28}/></div><div className="device-text"><span className="eyebrow">PLACA SELECIONADA</span><h2>{board?.deviceId || ble.current.deviceId || 'Nenhum dispositivo conectado'}</h2><p>{board ? `Firmware ${board.version} · 4 saídas digitais · 4 entradas digitais` : 'Conecte-se para ler estados e configurações em tempo real'}</p></div><div className="device-action">{connection === 'disconnected' ? <button className="primary" disabled={!supported || busy} onClick={connect}><Bluetooth size={18}/> Conectar BLE</button> : <button className="outline" onClick={() => ble.current.disconnect()}><BluetoothOff size={17}/> Desconectar</button>}</div></section>
      {connection === 'connected' && <section className="card auth-card"><div><h3><LockKeyhole size={18}/> Autenticação da placa</h3><p>Informe a chave individual de provisionamento gravada no ESP32. Ela não é salva no navegador.</p></div><div className="form-row"><label className="form-control"><span>Chave individual</span><input type="password" autoComplete="off" placeholder="32 caracteres ou mais" value={secret} onChange={e=>setSecret(e.target.value)} onKeyDown={e=>{if(e.key==='Enter' && !busy) void auth();}}/></label><button className="primary align-end" onClick={auth} disabled={busy || secret.trim().length < 32}><LockKeyhole size={17}/> Autenticar</button></div></section>}
      {tab === 'panel' && <><div className="section-heading"><div><h2>Visão geral</h2><p>Acompanhe os canais e seu estado atual.</p></div><button className="subtle-button" disabled={!authenticated || busy} onClick={()=>run(async()=>{await refresh();info('Estados sincronizados.');})}><RefreshCcw size={16}/> Atualizar</button></div>
        <div className="stats-grid"><div className="stat-card"><Radio size={20}/><small>Entradas ativas</small><strong>{board?.inputs.filter(Boolean).length ?? '—'} <span>/ 4</span></strong></div><div className="stat-card"><Power size={20}/><small>Saídas ligadas</small><strong>{board?.outputs.filter(Boolean).length ?? '—'} <span>/ 4</span></strong></div><div className="stat-card"><Wifi size={20}/><small>Rede Wi-Fi</small><strong className="word-status">{board ? (board.wifi.connected ? 'Conectado' : 'Offline') : '—'}</strong></div><div className="stat-card"><Cloud size={20}/><small>Broker MQTT</small><strong className="word-status">{board ? (board.mqtt.connected ? 'Conectado' : 'Offline') : '—'}</strong></div></div>
        {board && !board.physicalOutputsEnabled && <div className="callout warning"><CircleAlert size={19}/><span><strong>Saídas físicas bloqueadas.</strong> O firmware foi entregue com trava de segurança até a polaridade e o circuito de acionamento serem validados.</span></div>}
        <div className="columns-2"><section className="card"><h3><Power size={18}/> Saídas digitais</h3><p className="card-desc">Acione cada canal independentemente.</p><div className="channels">{[0,1,2,3].map(i => <div className="channel" key={i}><span className={`channel-dot ${board?.outputs[i] ? 'on' : ''}`}/><div className="channel-label"><strong>{board?.outputNames[i] || `Saída ${i+1}`}</strong><small>OUT{i+1} · GPIO {[26,25,33,32][i]}</small></div><label className="switch"><input type="checkbox" checked={!!board?.outputs[i]} disabled={!authenticated || busy || !board?.physicalOutputsEnabled} onChange={e=>changeOutput(i,e.target.checked)}/><span/></label></div>)}</div></section>
        <section className="card"><h3><Activity size={18}/> Entradas digitais</h3><p className="card-desc">Leitura de sinais externos do hardware.</p><div className="channels">{[0,1,2,3].map(i => <div className="channel" key={i}><span className={`channel-dot ${board?.inputs[i] ? 'on' : ''}`}/><div className="channel-label"><strong>{board?.inputNames[i] || `Entrada ${i+1}`}</strong><small>IN{i+1} · GPIO {[35,34,39,36][i]}</small></div><span className={`state-chip ${board?.inputs[i] ? 'high' : ''}`}>{board ? (board.inputs[i] ? 'ATIVO' : 'INATIVO') : '—'}</span></div>)}</div></section></div>
      </>}
      {tab === 'labels' && <section className="card form-card"><div className="card-title-row"><h3><Cable size={19}/> Nomes dos canais</h3><span className="tag">Identificadores MQTT fixos</span></div><p className="card-desc">Personalize os rótulos exibidos no aplicativo sem mudar os números e tópicos MQTT dos canais.</p><div className="two-form-columns"><div><h4>Saídas</h4>{outNames.map((n,i)=><label className="form-control" key={i}><span>Saída {i+1} · GPIO {[26,25,33,32][i]}</span><input maxLength={32} value={n} onChange={e=>changeName('out',i,e.target.value)}/></label>)}</div><div><h4>Entradas</h4>{inNames.map((n,i)=><label className="form-control" key={i}><span>Entrada {i+1} · GPIO {[35,34,39,36][i]}</span><input maxLength={32} value={n} onChange={e=>changeName('in',i,e.target.value)}/></label>)}</div></div><button className="primary" disabled={!authenticated||busy} onClick={()=>send({cmd:'set_labels',outputs:outNames,inputs:inNames},'Nomes salvos na memória da placa.')}><Save size={17}/> Salvar nomes</button></section>}
      {tab === 'network' && <section className="card form-card"><h3><Wifi size={19}/> Configuração Wi-Fi</h3><p className="card-desc">A senha é transmitida apenas pelo BLE e gravada na memória da placa. Não é recuperada de volta para a página.</p><div className="info-strip">{board?.wifi.connected ? <Signal size={18}/> : <WifiOff size={18}/>} <span>{board ? (board.wifi.connected ? `Conectado a ${board.wifi.ssid} · IP ${board.wifi.ip} · ${board.wifi.rssi} dBm` : 'Placa sem conexão Wi-Fi') : 'Conecte-se para consultar a rede atual'}</span></div><div className="form-grid"><label className="form-control"><span>Nome da rede (SSID)</span><input value={ssid} maxLength={32} placeholder="MinhaRede2G" onChange={e=>setSsid(e.target.value)}/></label><label className="form-control"><span>Senha Wi-Fi</span><input type="password" autoComplete="new-password" value={wifiPass} maxLength={64} placeholder="Nova senha" onChange={e=>setWifiPass(e.target.value)}/></label></div><p className="helper">Use Wi-Fi de 2,4 GHz. Ao salvar, o ESP32 tentará se conectar; a resposta BLE confirma o armazenamento, não garante acesso à rede.</p><button className="primary" disabled={!authenticated||busy||!ssid.trim()||wifiPass.length < 8} onClick={()=>send({cmd:'set_wifi',ssid:ssid.trim(),password:wifiPass},'Wi-Fi salvo. A placa iniciará a conexão.').then(()=>setWifiPass(''))}><Save size={17}/> Salvar Wi-Fi</button></section>}
      {tab === 'mqtt' && <section className="card form-card"><h3><Cloud size={19}/> Broker MQTT</h3><p className="card-desc">O ESP32 conecta diretamente ao broker configurado. Para TLS é obrigatório informar o certificado CA que valida o servidor.</p><div className="info-strip"><span className={`channel-dot ${board?.mqtt.connected ? 'on' : ''}`}/>{board ? (board.mqtt.connected ? 'Conectado ao broker MQTT' : 'Broker desconectado') : 'Conecte-se para consultar status MQTT'}</div><div className="form-grid"><label className="form-control"><span>Endereço do broker (sem protocolo)</span><input value={mqtt.host} maxLength={120} placeholder="mqtt.exemplo.com" onChange={e=>setMqtt(p=>({...p,host:e.target.value}))}/></label><label className="form-control"><span>Porta TCP</span><input type="number" min={1} max={65535} value={mqtt.port} onChange={e=>setMqtt(p=>({...p,port:Number(e.target.value)}))}/></label><label className="form-control"><span>Usuário MQTT</span><input value={mqtt.user} maxLength={64} onChange={e=>setMqtt(p=>({...p,user:e.target.value}))}/></label><label className="form-control"><span>Senha MQTT (digite a nova senha para salvar)</span><input type="password" autoComplete="new-password" value={mqtt.password} onChange={e=>setMqtt(p=>({...p,password:e.target.value}))}/></label><label className="form-control full"><span>Prefixo dos tópicos</span><input value={mqtt.topicPrefix} maxLength={100} placeholder="codecontrol/meu-dispositivo" onChange={e=>setMqtt(p=>({...p,topicPrefix:e.target.value}))}/><small>Exemplo: prefixo/cmd/out/1 e prefixo/state/in/1</small></label></div><label className="checkbox-line"><input type="checkbox" checked={mqtt.tls} onChange={e=>setMqtt(p=>({...p,tls:e.target.checked,port:e.target.checked?8883:1883}))}/><span>Usar TLS com validação do certificado do broker (recomendado)</span></label>{mqtt.tls && <label className="form-control"><span>Certificado CA PEM (obrigatório na primeira configuração TLS)</span><textarea rows={6} spellCheck={false} placeholder={'-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----'} value={mqtt.caPem} onChange={e=>setMqtt(p=>({...p,caPem:e.target.value}))}/><small>Não é possível recuperar o CA salvo pelo navegador. Se já estiver configurado, deixe vazio para mantê-lo.</small></label>}<button className="primary" disabled={!authenticated||busy||!mqtt.host.trim()||!mqtt.topicPrefix.trim()||mqtt.port<1||mqtt.port>65535} onClick={()=>send({cmd:'set_mqtt',host:mqtt.host.trim(),port:mqtt.port,tls:mqtt.tls,user:mqtt.user,password:mqtt.password,topicPrefix:mqtt.topicPrefix.trim(),caPem:mqtt.caPem},'Configuração MQTT salva na placa.').then(()=>setMqtt(p=>({...p,password:'',caPem:''})))}><Save size={17}/> Salvar MQTT</button></section>}
      {tab === 'service' && <><section className="card form-card"><h3><Settings2 size={19}/> Diagnóstico e manutenção</h3><div className="diagnostics"><div><span>Identificador</span><strong>{board?.deviceId || '—'}</strong></div><div><span>Versão do firmware</span><strong>{board?.version || '—'}</strong></div><div><span>Saídas físicas</span><strong>{board ? (board.physicalOutputsEnabled ? 'Habilitadas' : 'Bloqueadas para validação') : '—'}</strong></div></div><div className="button-row"><button className="outline" disabled={!authenticated||busy} onClick={()=>run(async()=>{await refresh();info('Diagnóstico atualizado.');})}><RefreshCcw size={17}/> Ler status</button><button className="outline" disabled={!authenticated||busy} onClick={()=>send({cmd:'restart'},'Comando de reinicialização enviado.',false)}><RotateCcw size={17}/> Reiniciar ESP32</button></div></section><section className="card form-card danger-zone"><h3><CircleAlert size={19}/> Redefinição</h3><p className="card-desc">A redefinição só pode ser solicitada durante sessão BLE autorizada. Para evitar exclusão acidental, será exigida confirmação adicional.</p><div className="radio-row"><label><input type="radio" checked={resetChoice==='wifi'} onChange={()=>setResetChoice('wifi')}/> Remover somente Wi-Fi</label><label><input type="radio" checked={resetChoice==='factory'} onChange={()=>setResetChoice('factory')}/> Restaurar configuração de fábrica</label></div><button className="danger-button" disabled={!authenticated||busy} onClick={()=>{const text = resetChoice==='factory' ? 'Apagar TODOS os nomes, Wi-Fi e MQTT desta placa?' : 'Apagar a configuração Wi-Fi desta placa?';if(window.confirm(text))void send({cmd:resetChoice==='factory'?'factory_reset':'reset_wifi',confirm:'RESET'},'Reset solicitado; o ESP32 reiniciará.',false);}}><RotateCcw size={17}/> Executar redefinição</button></section><section className="card form-card"><h3><Activity size={18}/> Registro desta sessão</h3><div className="log-box">{logs.length?logs.map((line,i)=><div key={i}>{line}</div>):<span>Nenhum evento registrado.</span>}</div></section></>}
      <footer><div><ShieldCheck size={16}/> Operação local · Web Bluetooth</div><span>CodeControl 4IO · Versão experimental para validação em bancada</span></footer>
    </main>
  </div>;
}
