# Integração MQTT

O `topicPrefix` é configurado pelo cliente no navegador, por exemplo `codecontrol/CC4IO-ABC123`. Os nomes amigáveis de I/O não alteram esses tópicos.

| Tópico | Direção ESP32 | Exemplo de payload | Retido |
|---|---|---|---|
| `<prefix>/availability` | publicar | `online` ou `offline` (LWT) | sim |
| `<prefix>/state/out/1` até `/4` | publicar | `0` ou `1` | sim |
| `<prefix>/state/in/1` até `/4` | publicar | `0` ou `1` | sim |
| `<prefix>/cmd/out/1` até `/4` | assinar | `1`, `0`, `ON`, `OFF`, `TRUE`, `FALSE` | evitar |

**Exemplo:** para ligar OUT1, publique `ON` em `codecontrol/CC4IO-ABC123/cmd/out/1`. Se `ALLOW_PHYSICAL_OUTPUTS=false`, a solicitação é ignorada fisicamente, inclusive por MQTT.

**TLS:** use porta 8883 (ou sua porta TLS), verifique o hostname do broker no certificado e informe o certificado raiz/CA PEM correto no app. A conexão é recusada sem CA no modo TLS.

**Sem TLS:** a porta 1883 é aceita para um broker de testes em rede confiável; neste modo as credenciais MQTT transitam sem criptografia. Nunca exponha a porta 1883 à internet sem proteção.

**ACL do broker:** restrinja cada cliente aos tópicos do próprio prefixo. Não reutilize o mesmo usuário/senha MQTT em dispositivos de clientes diferentes.

**Observações v0.1:** a placa reporta estados ao broker; o site Web Bluetooth **não** assina MQTT remotamente. Para controle remoto pelo navegador será preciso integrar uma API autenticada ou MQTT sobre WebSockets ao front-end em uma etapa futura.
