# Segurança e prontidão para produção

## Implementado nesta entrega

- Janela de provisionamento BLE fechada após 5 minutos de inatividade após configuração.
- Botão físico abre nova janela; sem Wi-Fi configurado, BLE anuncia automaticamente.
- Chave individual da placa (32+ caracteres) guardada apenas no firmware.
- Challenge HMAC-SHA256 para autenticação de comandos; o nonce é renovado a cada conexão.
- Bloqueio de saídas físicas por padrão; depende de habilitação explícita após validação elétrica.
- Senhas e chave não são enviadas em respostas de status nem persistidas no navegador.
- CA PEM obrigatório quando MQTT TLS está ativado.

## PENDENTE antes de entregar a clientes

1. **Criptografia do link BLE com pareamento seguro e MITM**: o HMAC autentica comandos, mas não cifra mensagens. Senhas Wi-Fi/MQTT inseridas durante sessão não criptografada podem ser capturadas por um observador próximo. Não use credenciais reais de clientes nesta versão experimental.
2. Verificar suporte real do Android/Chrome e fluxo de pareamento/reauth na placa física.
3. Habilitar NVS Encryption e Secure Boot/Flash Encryption conforme capacidade da placa e processo de fabricação. Hoje o ESP32 armazena os segredos em Preferences sem cifragem pela aplicação.
4. Validar reset e recuperação sem autorizar posse indevida após roubo/perda do aparelho.
5. Bloqueio progressivo e backoff para falhas de autenticação, além das cinco tentativas por conexão.
6. Auditoria de comandos críticos e políticas de autorização do broker por dispositivo.
7. Failsafe elétrico independente do firmware, incluindo estados durante boot, watchdog, OTA e falhas de alimentação.
8. Ensaios de compatibilidade EMI, fontes 12/24 V, isolamento, ruídos de entrada e capacidade de chaveamento do circuito real.
9. Testes automatizados de protocolo, testes em hardware (com carga fictícia), teste de desconexão em meio à gravação e MQTT sob perda de rede.

## Procedimento recomendado

Antes de liberar `ALLOW_PHYSICAL_OUTPUTS`, confirmar por medição o nível lógico ativo/inativo para os quatro drivers, presença de pull-up/down de hardware, estado de power-on e comportamento das cargas. Só depois conectar equipamentos reais.
