/*
  CodeControl 4IO · ESP32 clássico (Arduino Core 2.0.17 / PlatformIO 6.12.0)
  Web Bluetooth: JSON delimitado por \n, fragmentado em pacotes de 20 bytes.
  ATENCAO: prototipo de bancada; ver README/SECURITY antes de uso comercial.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <mbedtls/md.h>
#include <esp_system.h>
#include "hardware.h"
#include "private_config.h"

static const char* FW_VERSION = "0.1.0-beta";
static const char* SERVICE_UUID = "8a12f100-587c-4c9f-9603-2f83c1a64001";
static const char* WRITE_UUID   = "8a12f101-587c-4c9f-9603-2f83c1a64001";
static const char* NOTIFY_UUID  = "8a12f102-587c-4c9f-9603-2f83c1a64001";
static const size_t MAX_REQUEST_BYTES = 4096;

Preferences prefs;
BLEServer* bleServer = nullptr;
BLECharacteristic* bleReply = nullptr;
SemaphoreHandle_t rxMutex = nullptr;
String rxPartial, pendingRequest;
bool bleActive = false;
volatile bool bleConnected = false;
volatile bool bleAuthenticated = false;
uint32_t bleWindowStarted = 0;
uint8_t authAttempts = 0;
String deviceId, challenge;
uint32_t restartAt = 0;

WiFiClient plainTransport;
WiFiClientSecure tlsTransport;
PubSubClient plainMqtt(plainTransport);
PubSubClient secureMqtt(tlsTransport);
PubSubClient* mqttClient = &plainMqtt;
String wifiSsid, wifiPassword, mqttHost, mqttUser, mqttPassword, topicPrefix, mqttCaPem;
uint16_t mqttPort = 8883;
bool mqttTls = true;
bool outputStates[4] = {false, false, false, false};
bool inputStates[4] = {false, false, false, false};
String outNames[4], inNames[4];
uint32_t wifiLastTry = 0, mqttLastTry = 0, lastInputSample = 0;
uint32_t buttonStarted = 0;
bool buttonHandled = false;

static String hexify(const unsigned char* data, size_t n) {
  static const char* H = "0123456789abcdef";
  String out; out.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) { out += H[(data[i] >> 4) & 15]; out += H[data[i] & 15]; }
  return out;
}

static String nonceHex() {
  unsigned char bytes[16];
  for (uint8_t i = 0; i < sizeof(bytes); i += 4) {
    uint32_t number = esp_random();
    memcpy(bytes + i, &number, 4);
  }
  return hexify(bytes, sizeof(bytes));
}

static String makeDeviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "CC4IO-%06llX", (unsigned long long)(mac & 0xFFFFFFULL));
  return String(buffer);
}

static bool validSecret() {
  String secret = String(PROVISIONING_SECRET);
  return secret.length() >= 32 && secret.indexOf("INSIRA_AQUI") < 0;
}

static bool verifyHmac(String proof) {
  if (!validSecret() || challenge.isEmpty() || proof.length() != 64) return false;
  String message = deviceId + ":" + challenge;
  const mbedtls_md_info_t* sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  unsigned char digest[32] = {0};
  if (!sha) return false;
  int result = mbedtls_md_hmac(sha,
    (const unsigned char*)PROVISIONING_SECRET, strlen(PROVISIONING_SECRET),
    (const unsigned char*)message.c_str(), message.length(), digest);
  if (result != 0) return false;
  proof.toLowerCase();
  // Comparacao em tempo constante para nao vazar prefixos da prova.
  String expected = hexify(digest, sizeof(digest));
  unsigned char diff = 0;
  for (size_t i = 0; i < 64; ++i) diff |= (unsigned char)(expected[i] ^ proof[i]);
  return diff == 0;
}

static void loadSettings() {
  wifiSsid = prefs.getString("ssid", "");
  wifiPassword = prefs.getString("wpass", "");
  mqttHost = prefs.getString("host", "");
  mqttPort = prefs.getUShort("port", 8883);
  mqttTls = prefs.getBool("tls", true);
  mqttUser = prefs.getString("muser", "");
  mqttPassword = prefs.getString("mpass", "");
  mqttCaPem = prefs.getString("ca", "");
  topicPrefix = prefs.getString("prefix", "codecontrol/" + deviceId);
  for (uint8_t i = 0; i < 4; ++i) {
    outNames[i] = prefs.getString((String("out") + i).c_str(), "Saída " + String(i + 1));
    inNames[i] = prefs.getString((String("in") + i).c_str(), "Entrada " + String(i + 1));
  }
}

static void applyMqttSettings() {
  plainMqtt.disconnect(); secureMqtt.disconnect();
  mqttClient = mqttTls ? &secureMqtt : &plainMqtt;
  if (mqttTls && mqttCaPem.length()) tlsTransport.setCACert(mqttCaPem.c_str());
  mqttClient->setServer(mqttHost.c_str(), mqttPort);
  mqttClient->setBufferSize(512);
}

static void publishState(bool includeInputs = true) {
  if (!mqttClient->connected()) return;
  String avail = topicPrefix + "/availability";
  mqttClient->publish(avail.c_str(), "online", true);
  for (uint8_t i = 0; i < 4; ++i) {
    String name = topicPrefix + "/state/out/" + String(i+1);
    mqttClient->publish(name.c_str(), outputStates[i] ? "1" : "0", true);
    if (includeInputs) {
      name = topicPrefix + "/state/in/" + String(i+1);
      mqttClient->publish(name.c_str(), inputStates[i] ? "1" : "0", true);
    }
  }
}

static bool setPhysicalOutput(uint8_t index, bool value) {
  if (index >= 4 || !ALLOW_PHYSICAL_OUTPUTS) return false;
  uint8_t inactive = OUTPUT_ACTIVE_LEVEL == HIGH ? LOW : HIGH;
  digitalWrite(OUTPUT_PINS[index], value ? OUTPUT_ACTIVE_LEVEL : inactive);
  outputStates[index] = value;
  publishState(false);
  return true;
}

static void onMqttMessage(char* topic, uint8_t* payload, unsigned int length) {
  String current(topic);
  String head = topicPrefix + "/cmd/out/";
  if (!current.startsWith(head) || length > 12 || length == 0) return;
  int channel = current.substring(head.length()).toInt();
  if (channel < 1 || channel > 4) return;
  String value;
  for (unsigned int i=0; i<length; ++i) value += (char)payload[i];
  value.trim(); value.toUpperCase();
  if (value == "1" || value == "ON" || value == "TRUE") setPhysicalOutput(channel-1, true);
  else if (value == "0" || value == "OFF" || value == "FALSE") setPhysicalOutput(channel-1, false);
}

static void maintainNetwork() {
  uint32_t now = millis();
  if (!wifiSsid.length()) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (now - wifiLastTry > 12000 || wifiLastTry == 0) {
      wifiLastTry = now;
      WiFi.mode(WIFI_STA);
      WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
      Serial.println("[WiFi] Tentando conectar...");
    }
    return;
  }
  if (!mqttHost.length() || (mqttTls && !mqttCaPem.length())) return;
  if (!mqttClient->connected()) {
    if (now - mqttLastTry < 10000 && mqttLastTry != 0) return;
    mqttLastTry = now;
    String availabilityTopic = topicPrefix + "/availability";
    bool ok = mqttClient->connect(deviceId.c_str(),
      mqttUser.length() ? mqttUser.c_str() : nullptr,
      mqttUser.length() ? mqttPassword.c_str() : nullptr,
      availabilityTopic.c_str(), 0, true, "offline");
    if (ok) {
      mqttClient->subscribe((topicPrefix + "/cmd/out/+").c_str());
      publishState();
      Serial.println("[MQTT] Conectado.");
    } else Serial.printf("[MQTT] Falha: %d\n", mqttClient->state());
  } else mqttClient->loop();
}

static void startAdvertisingWindow() {
  bleWindowStarted = millis();
  bleAuthenticated = false;
  authAttempts = 0;
  challenge = nonceHex();
  bleActive = true;
  BLEDevice::getAdvertising()->start();
  Serial.println("[BLE] Configuracao disponivel.");
}

static void monitorButton() {
  if (CONFIG_BUTTON_PIN < 0) return;
  uint32_t now = millis();
  if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
    if (!buttonStarted) buttonStarted = now;
    if (!buttonHandled && now - buttonStarted >= 3000) {
      buttonHandled = true;
      startAdvertisingWindow();
    }
  } else { buttonStarted = 0; buttonHandled = false; }
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { bleConnected = true; bleAuthenticated = false; authAttempts = 0; challenge = nonceHex(); }
  void onDisconnect(BLEServer*) override {
    bleConnected = false; bleAuthenticated = false;
    if (bleActive) BLEDevice::startAdvertising();
  }
};

class ReceiveCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    std::string part = characteristic->getValue();
    if (xSemaphoreTake(rxMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    for (char c : part) {
      if (c == '\n') {
        if (pendingRequest.isEmpty()) pendingRequest = rxPartial;
        rxPartial = "";
      } else if (rxPartial.length() < MAX_REQUEST_BYTES) rxPartial += c;
      else rxPartial = ""; // Descarta quadro grande demais.
    }
    xSemaphoreGive(rxMutex);
  }
};

static void sendBLE(const JsonDocument& doc) {
  if (!bleConnected) return;
  String response;
  serializeJson(doc, response);
  response += '\n';
  for (size_t i = 0; i < response.length() && bleConnected; i += 20) {
    size_t n = min((size_t)20, response.length() - i);
    bleReply->setValue((uint8_t*)response.c_str() + i, n);
    bleReply->notify();
    delay(16);
  }
}

static void respondError(int id, const String& error) {
  StaticJsonDocument<256> out;
  out["id"] = id; out["ok"] = false; out["error"] = error;
  sendBLE(out);
}

static void respondOk(int id, const char* message = nullptr) {
  StaticJsonDocument<192> out;
  out["id"] = id; out["ok"] = true;
  if (message) out["message"] = message;
  sendBLE(out);
}

static void sendCurrentState(int id) {
  DynamicJsonDocument out(2560);
  out["id"] = id; out["ok"] = true;
  JsonObject state = out.createNestedObject("state");
  state["deviceId"] = deviceId;
  state["version"] = FW_VERSION;
  state["physicalOutputsEnabled"] = ALLOW_PHYSICAL_OUTPUTS;
  JsonArray outputs = state.createNestedArray("outputs");
  JsonArray inputs  = state.createNestedArray("inputs");
  JsonArray outputNames = state.createNestedArray("outputNames");
  JsonArray inputNames = state.createNestedArray("inputNames");
  for (uint8_t i = 0; i < 4; ++i) {
    outputs.add(outputStates[i]); inputs.add(inputStates[i]);
    outputNames.add(outNames[i]); inputNames.add(inNames[i]);
  }
  JsonObject wifi = state.createNestedObject("wifi");
  wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["ssid"] = wifiSsid;
  wifi["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  wifi["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  JsonObject mqtt = state.createNestedObject("mqtt");
  mqtt["connected"] = mqttClient->connected();
  mqtt["host"] = mqttHost; mqtt["port"] = mqttPort;
  mqtt["tls"] = mqttTls; mqtt["user"] = mqttUser;
  mqtt["topicPrefix"] = topicPrefix;
  // NAO devolver senhas, segredo de provisionamento ou certificado TLS.
  sendBLE(out);
}

static bool validateTopicPrefix(const String& prefix) {
  if (prefix.isEmpty() || prefix.length() > 100) return false;
  if (prefix.startsWith("/") || prefix.endsWith("/")) return false;
  for (size_t i=0; i<prefix.length(); ++i) {
    char c = prefix[i];
    if (c == '+' || c == '#' || c == ' ' || c == '\n' || c == '\r') return false;
  }
  return true;
}

static void handleCommand(const String& request) {
  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, request) != DeserializationError::Ok) { respondError(-1, "JSON invalido"); return; }
  int id = doc["id"] | -1;
  String cmd = doc["cmd"] | "";
  if (cmd == "hello") {
    challenge = nonceHex();
    StaticJsonDocument<256> answer;
    answer["id"] = id; answer["ok"] = true; answer["deviceId"] = deviceId;
    answer["nonce"] = challenge; answer["version"] = FW_VERSION;
    sendBLE(answer); return;
  }
  if (cmd == "auth") {
    if (authAttempts >= 5) { respondError(id, "Limite de tentativas. Reinicie a janela BLE pelo botao."); return; }
    ++authAttempts;
    if (!verifyHmac(doc["proof"] | "")) { respondError(id, "Chave incorreta ou chave nao gravada no firmware"); return; }
    bleAuthenticated = true; challenge = "";
    respondOk(id, "Autenticado"); return;
  }
  if (!bleAuthenticated || !bleActive) { respondError(id, "Autenticacao BLE obrigatoria"); return; }
  // Renova a janela para uma sessao ativa de configuracao.
  bleWindowStarted = millis();
  if (cmd == "state") { sendCurrentState(id); return; }
  if (cmd == "set_output") {
    int channel = doc["channel"] | 0;
    if (channel < 1 || channel > 4 || !doc["value"].is<bool>()) { respondError(id, "Canal/valor invalido"); return; }
    if (!ALLOW_PHYSICAL_OUTPUTS) { respondError(id, "Saidas bloqueadas: validar circuito e hardware.h"); return; }
    setPhysicalOutput(channel - 1, doc["value"].as<bool>());
    respondOk(id); return;
  }
  if (cmd == "set_labels") {
    JsonArray outs = doc["outputs"].as<JsonArray>();
    JsonArray ins  = doc["inputs"].as<JsonArray>();
    if (outs.size() != 4 || ins.size() != 4) { respondError(id, "Esperados 4 nomes de saida e 4 de entrada"); return; }
    for (uint8_t i=0; i<4; ++i) {
      if (!outs[i].is<const char*>() || !ins[i].is<const char*>() ||
          strlen(outs[i].as<const char*>()) > 64 || strlen(ins[i].as<const char*>()) > 64) {
        respondError(id, "Nome invalido ou maior que 64 bytes"); return;
      }
    }
    for (uint8_t i=0; i<4; ++i) {
      outNames[i] = outs[i].as<String>(); inNames[i] = ins[i].as<String>();
      prefs.putString((String("out")+i).c_str(), outNames[i]);
      prefs.putString((String("in")+i).c_str(), inNames[i]);
    }
    respondOk(id); return;
  }
  if (cmd == "set_wifi") {
    String newSsid = doc["ssid"] | "";
    String newPassword = doc["password"] | "";
    if (newSsid.isEmpty() || newSsid.length() > 32 || newPassword.length() < 8 || newPassword.length() > 64) {
      respondError(id, "SSID/senha invalida. Senha WPA2 de 8 a 64 caracteres."); return;
    }
    wifiSsid = newSsid; wifiPassword = newPassword;
    prefs.putString("ssid", wifiSsid); prefs.putString("wpass", wifiPassword);
    mqttClient->disconnect(); WiFi.disconnect();
    wifiLastTry = 0;
    respondOk(id, "Wi-Fi gravado; conexao em andamento"); return;
  }
  if (cmd == "set_mqtt") {
    String newHost = doc["host"] | "";
    String newUser = doc["user"] | "";
    String newPass = doc["password"] | "";
    String newPrefix = doc["topicPrefix"] | "";
    String newCa = doc["caPem"] | "";
    int newPort = doc["port"] | 0;
    bool newTls = doc["tls"] | true;
    if (newHost.isEmpty() || newHost.length() > 120 || newHost.indexOf('/') >= 0 || newHost.indexOf(' ') >= 0 ||
        newUser.length() > 64 || newPass.length() > 128 || newPort < 1 || newPort > 65535 ||
        !validateTopicPrefix(newPrefix)) { respondError(id, "Parametros MQTT invalidos"); return; }
    if (newTls && !newCa.length() && !mqttCaPem.length()) { respondError(id, "TLS exige certificado CA PEM"); return; }
    if (newCa.length() && (newCa.indexOf("-----BEGIN CERTIFICATE-----") < 0 || newCa.indexOf("-----END CERTIFICATE-----") < 0)) {
      respondError(id, "Certificado CA PEM invalido"); return;
    }
    // senha vazia nao remove senha antiga; redefinicao da senha exige nova senha.
    mqttHost = newHost; mqttPort = (uint16_t)newPort; mqttTls = newTls;
    mqttUser = newUser; topicPrefix = newPrefix;
    if (newPass.length()) mqttPassword = newPass;
    if (newCa.length()) mqttCaPem = newCa;
    prefs.putString("host", mqttHost); prefs.putUShort("port", mqttPort);
    prefs.putBool("tls", mqttTls); prefs.putString("muser", mqttUser);
    prefs.putString("mpass", mqttPassword); prefs.putString("prefix", topicPrefix);
    if (newCa.length()) prefs.putString("ca", mqttCaPem);
    applyMqttSettings(); mqttLastTry = 0;
    respondOk(id, "MQTT gravado"); return;
  }
  if (cmd == "restart") { respondOk(id, "Reiniciando"); restartAt = millis() + 1500; return; }
  if (cmd == "reset_wifi" || cmd == "factory_reset") {
    String confirm = doc["confirm"] | "";
    if (confirm != "RESET") { respondError(id, "Confirmacao RESET obrigatoria"); return; }
    if (cmd == "factory_reset") prefs.clear();
    else { prefs.remove("ssid"); prefs.remove("wpass"); }
    respondOk(id, "Reset solicitado");
    restartAt = millis() + 1500; return;
  }
  respondError(id, "Comando desconhecido");
}

static void setupBLE() {
  BLEDevice::init(deviceId.c_str());
  BLEDevice::setMTU(247);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());
  BLEService* service = bleServer->createService(SERVICE_UUID);
  BLECharacteristic* receive = service->createCharacteristic(WRITE_UUID, BLECharacteristic::PROPERTY_WRITE);
  receive->setCallbacks(new ReceiveCallbacks());
  bleReply = service->createCharacteristic(NOTIFY_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  bleReply->addDescriptor(new BLE2902());
  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  // A publicidade BLE permanece fechada fora da janela de configuracao.
  if (wifiSsid.isEmpty()) startAdvertisingWindow();
}

void setup() {
  Serial.begin(115200);
  delay(250);
  deviceId = makeDeviceId();
  prefs.begin("cc4io", false);
  loadSettings();

  // Saidas fisicas permanecem em alta impedancia quando nao validadas.
  const uint8_t inactive = OUTPUT_ACTIVE_LEVEL == HIGH ? LOW : HIGH;
  for (uint8_t i=0; i<4; ++i) {
    if (ALLOW_PHYSICAL_OUTPUTS) {
      digitalWrite(OUTPUT_PINS[i], inactive);
      pinMode(OUTPUT_PINS[i], OUTPUT);
    } else pinMode(OUTPUT_PINS[i], INPUT);
    pinMode(INPUT_PINS[i], INPUT); // 34/35/36/39 exigem resistor EXTERNO.
    inputStates[i] = (digitalRead(INPUT_PINS[i]) == (INPUT_ACTIVE_LOW ? LOW : HIGH));
  }
  if (CONFIG_BUTTON_PIN >= 0) pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  rxMutex = xSemaphoreCreateMutex();
  WiFi.mode(WIFI_STA);
  applyMqttSettings();
  plainMqtt.setCallback(onMqttMessage);
  secureMqtt.setCallback(onMqttMessage);
  setupBLE();
  Serial.printf("[BOOT] %s firmware %s | saídas %s\n", deviceId.c_str(), FW_VERSION,
    ALLOW_PHYSICAL_OUTPUTS ? "HABILITADAS" : "BLOQUEADAS");
  if (!validSecret()) Serial.println("[SECURITY] Configure include/private_config.h antes do uso!");
}

void loop() {
  monitorButton();
  if (bleActive && wifiSsid.length() && millis() - bleWindowStarted > BLE_WINDOW_MS) {
    bleAuthenticated = false;
    if (!bleConnected) {
      BLEDevice::getAdvertising()->stop();
      bleActive = false;
    }
  }
  String request;
  if (xSemaphoreTake(rxMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (pendingRequest.length()) { request = pendingRequest; pendingRequest = ""; }
    xSemaphoreGive(rxMutex);
  }
  if (request.length()) handleCommand(request);
  if (millis() - lastInputSample > 80) {
    lastInputSample = millis();
    for (uint8_t i=0; i<4; ++i) {
      bool current = (digitalRead(INPUT_PINS[i]) == (INPUT_ACTIVE_LOW ? LOW : HIGH));
      if (current != inputStates[i]) {
        inputStates[i] = current;
        if (mqttClient->connected()) {
          String path = topicPrefix + "/state/in/" + String(i+1);
          mqttClient->publish(path.c_str(), current ? "1" : "0", true);
        }
      }
    }
  }
  maintainNetwork();
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();
  delay(8);
}
