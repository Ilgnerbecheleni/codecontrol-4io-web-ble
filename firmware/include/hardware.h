#pragma once
#include <Arduino.h>

// Pinout copiado da imagem do projeto (ESP32 classico):
constexpr uint8_t OUTPUT_PINS[4] = {26, 25, 33, 32};
constexpr uint8_t INPUT_PINS[4]  = {35, 34, 39, 36}; // VN=39, VP=36

// AJUSTAR SOMENTE APOS VALIDAR O CIRCUITO ELETRICO COM MULTIMETRO/CARGA TESTE.
// 0 = firmware NAO energiza nenhuma saida fisica; comandos sao recusados.
// 1 = permite acionamento fisico; nunca habilitar sem confirmar hardware.
constexpr bool ALLOW_PHYSICAL_OUTPUTS = false;

// AJUSTAR conforme o circuito da placa: HIGH ou LOW para ligar relé/MOSFET.
constexpr uint8_t OUTPUT_ACTIVE_LEVEL = HIGH;

// GPIO 34/35/36/39 nao possuem pull-up/pull-down interno.
// O esquema da PCB PRECISA fornecer niveis validos (3,3 V max no pino ESP32).
constexpr bool INPUT_ACTIVE_LOW = false;

// Botao BOOT do DevKit: permite abrir BLE por 5min segurando por 3s.
// CUIDADO: GPIO0 pressionado ao ligar pode colocar ESP32 em modo de gravacao.
// Se sua placa nao possuir esse botao, ALTERE para GPIO disponivel com botao
// ligado a GND e resistor pull-up (ou -1 para desabilitar abertura por botao).
constexpr int CONFIG_BUTTON_PIN = 0;
constexpr uint32_t BLE_WINDOW_MS = 300000;
