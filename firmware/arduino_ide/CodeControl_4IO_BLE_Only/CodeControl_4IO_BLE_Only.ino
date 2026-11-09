/*
  CODECONTROL 4IO - BLE ONLY v0.2.1 (Arduino IDE)
  Placa: ESP32 classico / ESP32 Dev Module.
  Bibliotecas: NimBLE-Arduino 2.x (recomendado 2.5.2) e ArduinoJson 6.x.
  Pacote de placa: esp32 by Espressif Systems 2.0.17.

  Saidas: GPIO 26,25,33,32    | Entradas: GPIO 35,34,39,36
  Nao usa Wi-Fi, MQTT, TLS, PubSubClient ou servidor Web local.
  Compatibilidade: painel Web Bluetooth CodeControl (mesmos UUIDs e HMAC).

  AVISO DE SEGURANCA:
  ENABLE_PHYSICAL_OUTPUTS=false ate VALIDAR eletricamente os drivers.
  Saidas em entrada/alta impedancia NAO garantem estado seguro em todas as PCBs;
  use resistores externos e protecoes apropriadas. Nao controle cargas perigosas
  sem intertravamentos independentes. BLE desta versao nao criptografa o link;
  HMAC autentica comandos, mas nao fornece sigilo/pairing seguro para producao.
*/

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <mbedtls/md.h>
#include <esp_system.h>

constexpr uint8_t OUT_GPIO[4] = {26, 25, 33, 32};
constexpr uint8_t IN_GPIO[4]  = {35, 34, 39, 36};
constexpr bool ENABLE_PHYSICAL_OUTPUTS = false;   // MUDAR para true APOS validar o circuito!
constexpr uint8_t OUTPUT_ACTIVE_LEVEL = HIGH;    // HIGH ou LOW conforme driver da placa
constexpr bool INPUT_ACTIVE_LOW = false;         // Configurar conforme circuito de entrada
constexpr int RESET_BUTTON_GPIO = 0;             // Botao BOOT do ESP32, em operacao
constexpr uint32_t RESET_HOLD_MS = 8000;         // Segurar 8s para limpar nomes (nao a chave)

static const char* VERSION = "0.2.1-ble-only";
static const char* SERVICE_UUID = "8a12f100-587c-4c9f-9603-2f83c1a64001";
static const char* WRITE_UUID   = "8a12f101-587c-4c9f-9603-2f83c1a64001";
static const char* NOTIFY_UUID  = "8a12f102-587c-4c9f-9603-2f83c1a64001";

Preferences prefs;
NimBLECharacteristic* txCharacteristic = nullptr;
SemaphoreHandle_t rxMutex = nullptr;
String rxAccumulated, rxPending, deviceId, nonce, secretKey;
String outputNames[4], inputNames[4];
bool outputState[4] = {false, false, false, false};
bool inputState[4] = {false, false, false, false};
volatile bool bleConnected = false;
volatile bool authenticated = false;
uint8_t authFailures = 0;
uint32_t rebootAt = 0, resetPressedAt = 0, lastInputRead = 0;
bool resetExecuted = false;

String hexOf(const uint8_t* bytes, size_t count) {
  const char* map = "0123456789abcdef";
  String result;
  result.reserve(count * 2);
  for (size_t i = 0; i < count; ++i) {
    result += map[bytes[i] >> 4];
    result += map[bytes[i] & 0x0f];
  }
  return result;
}

void randomBytes(uint8_t* bytes, size_t count) {
  for (size_t i = 0; i < count; i += 4) {
    uint32_t word = esp_random();
    size_t chunk = min((size_t)4, count - i);
    memcpy(bytes + i, &word, chunk);
  }
}

String makeNonce() {
  uint8_t bytes[16];
  randomBytes(bytes, sizeof(bytes));
  return hexOf(bytes, sizeof(bytes));
}

void loadNames() {
  for (int i = 0; i < 4; ++i) {
    outputNames[i] = prefs.getString((String("out") + i).c_str(), "Saida " + String(i + 1));
    inputNames[i] = prefs.getString((String("in") + i).c_str(), "Entrada " + String(i + 1));
  }
}

void resetNames() {
  for (int i = 0; i < 4; ++i) {
    prefs.remove((String("out") + i).c_str());
    prefs.remove((String("in") + i).c_str());
  }
  loadNames();
}

void setupSecret() {
  secretKey = prefs.getString("blekey", "");
  if (secretKey.length() == 64) return;
  uint8_t randomData[32];
  randomBytes(randomData, sizeof(randomData));
  secretKey = hexOf(randomData, sizeof(randomData));
  prefs.putString("blekey", secretKey);
  Serial.println("[BLE] Nova chave unica gerada.");
}

bool proofMatches(String proof) {
  if (secretKey.length() != 64 || nonce.isEmpty() || proof.length() != 64) return false;
  String message = deviceId + ":" + nonce;
  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md) return false;
  uint8_t digest[32];
  if (mbedtls_md_hmac(md, (const unsigned char*)secretKey.c_str(), secretKey.length(),
                      (const unsigned char*)message.c_str(), message.length(), digest)) return false;
  String expected = hexOf(digest, sizeof(digest));
  proof.toLowerCase();
  uint8_t difference = 0;
  for (size_t i = 0; i < 64; ++i) difference |= expected[i] ^ proof[i];
  return difference == 0;
}

// Protocolo UTF-8 JSON + newline, em fragmentos de 20 bytes por compatibilidade GATT.
void sendJson(const JsonDocument& doc) {
  if (!bleConnected || txCharacteristic == nullptr) return;
  String line;
  serializeJson(doc, line);
  line += '\n';
  for (size_t offset = 0; offset < line.length() && bleConnected; offset += 20) {
    size_t length = min((size_t)20, line.length() - offset);
    txCharacteristic->setValue((uint8_t*)line.c_str() + offset, length);
    txCharacteristic->notify();
    delay(20);
  }
}

void errorReply(int id, const char* error) {
  StaticJsonDocument<200> msg;
  msg["id"] = id; msg["ok"] = false; msg["error"] = error;
  sendJson(msg);
}

void successReply(int id, const char* message = nullptr) {
  StaticJsonDocument<160> msg;
  msg["id"] = id; msg["ok"] = true;
  if (message) msg["message"] = message;
  sendJson(msg);
}

void stateReply(int id) {
  DynamicJsonDocument response(1600);
  response["id"] = id;
  response["ok"] = true;
  JsonObject state = response.createNestedObject("state");
  state["deviceId"] = deviceId;
  state["version"] = VERSION;
  state["physicalOutputsEnabled"] = ENABLE_PHYSICAL_OUTPUTS;
  JsonArray outputs = state.createNestedArray("outputs");
  JsonArray inputs = state.createNestedArray("inputs");
  JsonArray outs = state.createNestedArray("outputNames");
  JsonArray ins = state.createNestedArray("inputNames");
  for (int i = 0; i < 4; ++i) {
    outputs.add(outputState[i]);
    inputs.add(inputState[i]);
    outs.add(outputNames[i]);
    ins.add(inputNames[i]);
  }
  sendJson(response);
}

void physicalOutput(uint8_t index, bool on) {
  if (!ENABLE_PHYSICAL_OUTPUTS || index > 3) return;
  digitalWrite(OUT_GPIO[index], on ? OUTPUT_ACTIVE_LEVEL : (OUTPUT_ACTIVE_LEVEL == HIGH ? LOW : HIGH));
  outputState[index] = on;
}

void handleJson(const String& line) {
  StaticJsonDocument<1100> request;
  if (deserializeJson(request, line) != DeserializationError::Ok) {
    errorReply(-1, "JSON invalido"); return;
  }
  int id = request["id"] | -1;
  String cmd = request["cmd"] | "";
  if (cmd == "hello") {
    nonce = makeNonce();
    authenticated = false;
    StaticJsonDocument<220> reply;
    reply["id"] = id; reply["ok"] = true; reply["deviceId"] = deviceId;
    reply["nonce"] = nonce; reply["version"] = VERSION;
    sendJson(reply); return;
  }
  if (cmd == "auth") {
    if (authFailures >= 5) { errorReply(id, "Tentativas excedidas: reconecte fisicamente"); return; }
    if (!proofMatches(request["proof"] | "")) {
      ++authFailures; errorReply(id, "Chave BLE incorreta"); return;
    }
    authenticated = true;
    nonce = "";
    successReply(id, "Autenticado"); return;
  }
  if (!authenticated) { errorReply(id, "Autenticacao BLE obrigatoria"); return; }

  if (cmd == "state") { stateReply(id); return; }
  if (cmd == "set_output") {
    int channel = request["channel"] | 0;
    if (channel < 1 || channel > 4 || !request["value"].is<bool>()) {
      errorReply(id, "Canal ou valor invalido"); return;
    }
    if (!ENABLE_PHYSICAL_OUTPUTS) {
      errorReply(id, "Saidas bloqueadas: valide o circuito e habilite no INO"); return;
    }
    physicalOutput((uint8_t)(channel - 1), request["value"].as<bool>());
    successReply(id); return;
  }
  if (cmd == "set_labels") {
    JsonArray outs = request["outputs"].as<JsonArray>();
    JsonArray ins  = request["inputs"].as<JsonArray>();
    if (outs.size() != 4 || ins.size() != 4) {
      errorReply(id, "Esperados 4 nomes de entrada e saida"); return;
    }
    for (int i = 0; i < 4; ++i) {
      if (!outs[i].is<const char*>() || !ins[i].is<const char*>() ||
          strlen(outs[i].as<const char*>()) > 32 || strlen(ins[i].as<const char*>()) > 32) {
        errorReply(id, "Nome invalido: maximo 32 bytes UTF-8"); return;
      }
    }
    for (int i = 0; i < 4; ++i) {
      outputNames[i] = outs[i].as<String>();
      inputNames[i] = ins[i].as<String>();
      prefs.putString((String("out") + i).c_str(), outputNames[i]);
      prefs.putString((String("in") + i).c_str(), inputNames[i]);
    }
    successReply(id, "Nomes gravados"); return;
  }
  if (cmd == "restart") {
    successReply(id, "Reiniciando"); rebootAt = millis() + 1500; return;
  }
  if (cmd == "factory_reset") {
    if (String(request["confirm"] | "") != "RESET") {
      errorReply(id, "Confirmacao RESET obrigatoria"); return;
    }
    // Nao troca a chave BLE no reset; evita deixar cliente trancado para fora.
    resetNames();
    successReply(id, "Nomes restaurados"); rebootAt = millis() + 1500; return;
  }
  errorReply(id, "Comando desconhecido");
}

class ServerEvents : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    bleConnected = true;
    authenticated = false;
    nonce = "";
    authFailures = 0;
  }
  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    bleConnected = false;
    authenticated = false;
    nonce = "";
    NimBLEDevice::startAdvertising();
  }
};

class ReceiveEvents : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
    std::string chunk = characteristic->getValue();
    if (rxMutex == nullptr || xSemaphoreTake(rxMutex, pdMS_TO_TICKS(40)) != pdTRUE) return;
    for (char ch : chunk) {
      if (ch == '\n') {
        if (rxPending.isEmpty()) rxPending = rxAccumulated;
        rxAccumulated = "";
      } else if (rxAccumulated.length() < 1024) {
        rxAccumulated += ch;
      } else {
        rxAccumulated = "";
      }
    }
    xSemaphoreGive(rxMutex);
  }
};

void setup() {
  Serial.begin(115200);
  delay(300);
  uint64_t mac = ESP.getEfuseMac();
  char boardName[22];
  snprintf(boardName, sizeof(boardName), "CC4IO-%06llX", (unsigned long long)(mac & 0xFFFFFFULL));
  deviceId = boardName;

  // Nunca colocar um GPIO com hardware desconhecido em HIGH/LOW ativamente.
  const uint8_t offLevel = OUTPUT_ACTIVE_LEVEL == HIGH ? LOW : HIGH;
  for (int i = 0; i < 4; ++i) {
    if (ENABLE_PHYSICAL_OUTPUTS) {
      digitalWrite(OUT_GPIO[i], offLevel);
      pinMode(OUT_GPIO[i], OUTPUT);
    } else {
      pinMode(OUT_GPIO[i], INPUT);
    }
    pinMode(IN_GPIO[i], INPUT); // 34/35/36/39 sem pullup; maximo 3,3 V!
    inputState[i] = digitalRead(IN_GPIO[i]) == (INPUT_ACTIVE_LOW ? LOW : HIGH);
  }
  if (RESET_BUTTON_GPIO >= 0) pinMode(RESET_BUTTON_GPIO, INPUT_PULLUP);

  prefs.begin("cc4io", false);
  loadNames();
  rxMutex = xSemaphoreCreateMutex();
  // Esta inicializacao de BLE prepara o RNG para secretKey unica.
  NimBLEDevice::init(deviceId.c_str());
  NimBLEDevice::setMTU(247);
  setupSecret();

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerEvents());
  NimBLEService* service = server->createService(SERVICE_UUID);
  NimBLECharacteristic* receive = service->createCharacteristic(WRITE_UUID, NIMBLE_PROPERTY::WRITE);
  receive->setCallbacks(new ReceiveEvents());
  txCharacteristic = service->createCharacteristic(NOTIFY_UUID, NIMBLE_PROPERTY::NOTIFY);
  service->start();
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->enableScanResponse(true);
  advertising->setName(deviceId.c_str());
  advertising->start();

  Serial.println("\n=== CODECONTROL 4IO BLE ONLY ===");
  Serial.print("Dispositivo: "); Serial.println(deviceId);
  Serial.print("Chave BLE (guardar com sigilo): "); Serial.println(secretKey);
  Serial.printf("Saidas fisicas: %s\n", ENABLE_PHYSICAL_OUTPUTS ? "ATIVAS" : "BLOQUEADAS");
  Serial.println("Use Chrome Android/Windows, abra o configurador e conecte BLE.");
}

void loop() {
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) ESP.restart();

  // Botao BOOT pressionado 8 segundos durante a operacao restaura nomes.
  // Segurar BOOT ao energizar pode colocar ESP32 no modo de gravacao.
  if (RESET_BUTTON_GPIO >= 0) {
    if (digitalRead(RESET_BUTTON_GPIO) == LOW) {
      if (resetPressedAt == 0) resetPressedAt = millis();
      if (!resetExecuted && millis() - resetPressedAt >= RESET_HOLD_MS) {
        resetExecuted = true;
        resetNames();
        Serial.println("[RESET] Nomes dos canais restaurados; chave BLE mantida.");
      }
    } else { resetPressedAt = 0; resetExecuted = false; }
  }

  if (millis() - lastInputRead >= 80) {
    lastInputRead = millis();
    for (int i = 0; i < 4; ++i) {
      inputState[i] = digitalRead(IN_GPIO[i]) == (INPUT_ACTIVE_LOW ? LOW : HIGH);
    }
  }

  String command;
  if (rxMutex && xSemaphoreTake(rxMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (rxPending.length()) { command = rxPending; rxPending = ""; }
    xSemaphoreGive(rxMutex);
  }
  if (command.length()) handleJson(command);
  delay(5);
}