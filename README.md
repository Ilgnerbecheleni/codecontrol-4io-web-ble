# CodeControl 4IO — Configurador Web Bluetooth + Firmware ESP32

Projeto inicial para configurar uma placa ESP32 clássica com **4 saídas e 4 entradas digitais**, acessando um site estático hospedado gratuitamente no **GitHub Pages** (Chrome Android, Chrome/Edge desktop). Sem app, sem Expo, sem servidor de aplicação e sem conta no serviço.

> **Maturidade: protótipo de bancada (0.1.0-beta). NÃO usar em cargas críticas, instalações industriais ou com credenciais de produção antes dos testes elétricos e reforço de segurança BLE descritos abaixo.**

## 1. O que funciona (arquitetura implementada)

- Web React + Vite + TypeScript, publicada como site estático pelo GitHub Actions.
- Web Bluetooth GATT com pacote JSON fragmentado em blocos de 20 bytes e resposta por notificações.
- Identificação e autenticação HMAC-SHA256 por segredo individual gravado em cada ESP32.
- Configuração de SSID/senha Wi-Fi; broker, usuário/senha, TLS e certificado CA PEM, prefixo MQTT.
- Personalização persistente dos nomes de 4 saídas e 4 entradas.
- Leitura de estado (painel sincronizado a cada ~3 s via BLE) e tentativa de acionamento das 4 saídas (bloqueadas no firmware padrão até validação).
- Leitura de 4 entradas, atualização de estados pelo MQTT e controle via tópicos MQTT.
- Reinício e reset Wi-Fi/fábrica autenticados.
- Modo BLE aberto na primeira configuração e janela temporária de 5 minutos aberta por botão físico (GPIO0/BOOT no DevKit; adaptar à sua PCB).
- Configurações salvas na NVS, sobrevivem ao desligamento.

**Importante:** este pacote ainda não foi validado em um ESP32 físico. O projeto web também não pôde ser compilado no ambiente de geração por indisponibilidade das dependências do npm; o workflow fará o build no GitHub. Revise mensagens de compilação antes de instalar.

## 2. Mapeamento do esquema enviado

| Sinal | GPIO do ESP32 | Observação |
|:--|--:|:--|
| OUT1 | 26 | Saída, ativa em HIGH ou LOW conforme hardware |
| OUT2 | 25 | Saída |
| OUT3 | 33 | Saída |
| OUT4 | 32 | Saída |
| IN1 | 35 | Somente entrada; **sem pull-up/pull-down interno** |
| IN2 | 34 | Somente entrada; sem pull-up/pull-down interno |
| IN3 | 39 (VN) | Somente entrada; sem pull-up/pull-down interno |
| IN4 | 36 (VP) | Somente entrada; sem pull-up/pull-down interno |

**Não conecte 12/24 V diretamente aos GPIOs.** O esquema de interface deve isolar/limitar a tensão a 0–3,3 V, com pull-up/pull-down EXTERNOS nas entradas. Antes de liberar saídas, valide polaridade, circuito MOSFET/relé, resistor externo de estado seguro e condição durante boot/reset.

## 3. Estrutura

```text
codecontrol-4io-web-ble/
├── web/
│   ├── src/App.tsx             Interface de controle/configuração
│   ├── src/ble.ts              Transporte BLE Web (fragmentação, HMAC)
│   ├── src/types.ts            Contrato tipado
│   └── package.json
├── firmware/
│   ├── platformio.ini          ESP32 / Arduino 2.0.17
│   ├── include/hardware.h      PINOUT, polaridade, trava de saídas, botão
│   ├── include/private_config.example.h
│   └── src/main.cpp            Firmware completo
├── .github/workflows/deploy.yml
└── docs/
    ├── PROTOCOL.md
    ├── MQTT.md
    └── SECURITY.md
```

## 4. Teste local do site

Instale Node.js 20+ e execute:

```bash
cd web
npm install
npm run dev
```

Abra `http://localhost:5173` **no computador** com Bluetooth. Para testar no celular, publique no Pages com HTTPS: endereços `http://192.168.x.x` não atendem ao requisito de contexto seguro da API Web Bluetooth.

## 5. Publicar no GitHub Pages

1. Crie um repositório **público** com nome, por exemplo, `codecontrol-4io-web-ble` em https://github.com/new. O Pages para repositórios privados depende do plano/permissões disponíveis.
2. Descompacte este pacote, entre na pasta principal e execute:

```bash
git init
git branch -M main
git add .
git commit -m "feat: configurador BLE ESP32 4IO"
git remote add origin https://github.com/SEU_USUARIO/codecontrol-4io-web-ble.git
git push -u origin main
```

3. No GitHub, abra **Settings > Pages > Build and deployment > Source: GitHub Actions**.
4. Abra a aba **Actions** e confira a execução `Publicar configurador no GitHub Pages`.
5. Acesse `https://SEU_USUARIO.github.io/codecontrol-4io-web-ble/` com Chrome Android.

O workflow está preparado para executar `npm install` e `npm run build` dentro de `web/`, entregando `web/dist` para o Pages. O Vite usa `base: './'` para servir assets mesmo em repositório do tipo `/nome-repo/`.

## 6. Instalar firmware com VS Code + PlatformIO

1. Abra a pasta **firmware** no VS Code com a extensão PlatformIO.
2. Copie `include/private_config.example.h` para `include/private_config.h` e substitua a chave por **uma chave única e aleatória com 32+ caracteres**. Não compartilhe nem faça commit da chave real.
3. Confira `include/hardware.h` para sua PCB, incluindo GPIO do botão, polaridade de entrada e nível ativo das saídas.
4. Clique **Build** e depois **Upload**, selecionando a porta do ESP32.
5. Abra **Monitor** a 115200 baud. O dispositivo aparece como `CC4IO-XXXXXX`.
6. Na primeira instalação, o ESP32 anuncia BLE enquanto o Wi-Fi não estiver configurado. Clique **Conectar BLE** no site, selecione a placa e insira a mesma chave do `private_config.h`.
7. Configure nomes, SSID/senha e MQTT. Reinicie para testar persistência.
8. Para abrir nova janela BLE com Wi-Fi já salvo, mantenha o botão de configuração pressionado por 3 segundos (por padrão, GPIO0/BOOT do DevKit).

**Atenção:** GPIO0 baixo durante o reset coloca o ESP32 em modo de gravação. Para uma placa comercial, reserve um GPIO diferente para o botão (ligado a GND + pull-up), confira se está livre e ajuste `CONFIG_BUTTON_PIN`.

## 7. Trava elétrica (IMPORTANTÍSSIMO)

O firmware vem com `ALLOW_PHYSICAL_OUTPUTS = false`. Isso impede comandos BLE **e MQTT** de acionarem saídas. O GPIO fica em modo de entrada/alta impedância até a validação. A PCB precisa garantir estado seguro por hardware (resistor externo de pull-up/down). Somente depois de testar a polaridade com multímetro e carga de bancada altere para `true`, ajuste `OUTPUT_ACTIVE_LEVEL` e grave o firmware novamente. A interface mostra se as saídas estão bloqueadas.

## 8. Como funciona a conexão BLE

- O dispositivo BLE só anuncia com Wi-Fi não provisionado ou após abrir a janela pelo botão.
- O site faz `hello`, recebe `deviceId` e `nonce` temporário e calcula `HMAC-SHA256(secret, "deviceId:nonce")` via WebCrypto.
- O firmware confere o HMAC, depois autoriza comandos na sessão BLE.
- O segredo não é salvo no site, nem retornado pela placa.
- Os dados seguem como JSON + LF, fragmentados em blocos de 20 bytes, com confirmação de escrita e resposta notificada.
- Ao desconectar, a autorização se perde.

**Segurança para produção:** HMAC autentica os comandos, mas **não criptografa a carga útil BLE por si só**. Antes de colocar senhas reais de clientes, implemente e valide pareamento BLE com criptografia/MITM, proteção de flash/NVS, fluxo seguro de recuperação e testes de penetração. Veja `docs/SECURITY.md`.

## 9. MQTT

Veja `docs/MQTT.md`. Tópicos usam o `topicPrefix` configurável. O broker deve ser acessível pela rede da placa. No modo TLS informe um CA PEM válido para o host (validação do certificado é exigida). Não existe integração obrigatória com a nuvem da CodeControl.

## 10. Limitações da v0.1

- Requer Chrome Android ou navegador desktop compatível com Web Bluetooth; não funciona no Safari iOS.
- Sem OTA, cadastro de usuários, histórico e controle remoto direto no site. Controle remoto por MQTT fica disponível no broker configurado, por outro cliente autorizado.
- Não há indicação de estado por LED porque o desenho enviado não define LED/status.
- Wi-Fi escaneado automaticamente não está implementado; SSID é digitado.
- Detecção de entradas usa amostragem periódica aproximada de 80 ms (não é captura rápida de pulsos).
- Não libera as saídas físicas até a validação do circuito.
- Testar firmware e app com um ESP32 real antes de uso de campo.

## Licença

MIT para o código do configurador; o uso físico do hardware é de responsabilidade do integrador.
