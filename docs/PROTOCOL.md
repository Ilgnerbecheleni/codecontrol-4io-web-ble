# Protocolo BLE v0.1

## GATT

- Serviço: `8a12f100-587c-4c9f-9603-2f83c1a64001`
- Escrita (WRITE/Write With Response): `8a12f101-587c-4c9f-9603-2f83c1a64001`
- Notificação (NOTIFY): `8a12f102-587c-4c9f-9603-2f83c1a64001`
- Mensagens UTF-8 no formato JSON de objeto, terminadas em `\n`.
- Cada quadro enviado é dividido em blocos consecutivos de no máximo 20 bytes. O receptor remonta até a quebra de linha. Limite de pedido: 4096 bytes.
- O campo `id` identifica a requisição e a resposta, com `ok` booleano e opcional `error`.
- O cliente envia um comando por vez e se inscreve nas notificações antes do primeiro comando.

### Primeiro contato e autenticação

```json
{"id":1,"cmd":"hello"}
```

Resposta (exemplo):

```json
{"id":1,"ok":true,"deviceId":"CC4IO-ABC123","nonce":"0123456789abcdef0123456789abcdef","version":"0.1.0-beta"}
```

Na página o usuário digita um segredo único da placa (32+ caracteres). O navegador assina o texto `CC4IO-ABC123:0123456789abcdef0123456789abcdef` com HMAC-SHA256 e a chave. O digest hexadecimal minúsculo é enviado:

```json
{"id":2,"cmd":"auth","proof":"<64 hex chars>"}
```

O comando `hello` é público; os demais exigem `auth` válido na sessão.

### Exemplos

```json
{"id":3,"cmd":"state"}
{"id":4,"cmd":"set_labels","outputs":["Sala","Ventilador","Bomba","Luz externa"],"inputs":["Porta","Presenca","Boia","Reserva"]}
{"id":5,"cmd":"set_wifi","ssid":"MinhaRede","password":"senhaWiFi123"}
{"id":6,"cmd":"set_mqtt","host":"mqtt.exemplo.com","port":8883,"tls":true,"user":"cc4io","password":"senhaBroker","topicPrefix":"codecontrol/cc4io-abc123","caPem":"-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----"}
{"id":7,"cmd":"set_output","channel":1,"value":true}
{"id":8,"cmd":"restart"}
{"id":9,"cmd":"reset_wifi","confirm":"RESET"}
{"id":10,"cmd":"factory_reset","confirm":"RESET"}
```

O firmware não devolve senhas Wi-Fi/MQTT nem o segredo da placa em `state`.

### Estado

```json
{
  "id":3,"ok":true,
  "state":{
    "deviceId":"CC4IO-ABC123","version":"0.1.0-beta",
    "physicalOutputsEnabled":false,
    "outputs":[false,false,false,false],"inputs":[false,true,false,true],
    "outputNames":["Sala","Ventilador","Bomba","Luz externa"],
    "inputNames":["Porta","Presenca","Boia","Reserva"],
    "wifi":{"connected":true,"ssid":"MinhaRede","ip":"192.168.1.9","rssi":-52},
    "mqtt":{"connected":false,"host":"mqtt.exemplo.com","port":8883,"tls":true,"user":"cc4io","topicPrefix":"codecontrol/cc4io-abc123"}
  }
}
```
