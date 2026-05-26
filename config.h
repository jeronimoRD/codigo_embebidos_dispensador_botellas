#pragma once

// =============================================================================
// config.h — Configuración de red y parámetros del sistema
// =============================================================================
//     Guardar a .gitignore antes de subir a GitHub.
//     Use config.example.h como plantilla pública.
// =============================================================================

// -------- RED WiFi --------
#define WIFI_SSID       "Jero"
#define WIFI_PASSWORD   "30329385"

// -------- BROKER MQTT --------
#define MQTT_SERVER     "10.113.26.62" 
#define MQTT_PORT       1883
#define MQTT_CLIENT_ID  "ESP32_Planta"
#define MQTT_USER       "iot_device"
#define MQTT_PASSWORD   "SecurePass123!"
#define MQTT_TOPIC_LOG  "planta/logs"

// -------- PINES --------
#define PIN_MOTOR_BANDA  5
#define PIN_BOMBA        18
#define PIN_IR           19
#define PIN_FLUJO         4

// -------- PARÁMETROS DE LLENADO --------
// 294 pulsos ≈ volumen objetivo del vaso 50ml.
#define PULSOS_OBJETIVO  294L

// -------- PARÁMETROS DE TIEMPO --------
#define MS_ESPERA_POST_LLENADO   3000   // ms que espera antes de expulsar
#define MS_DEBOUNCE_IR            500   // ms de debounce tras salir la botella
#define MS_DEBOUNCE_FLUJO           2   // ms de debounce en interrupción de flujo
#define MS_ESPERA_GOTEO          2000   // ms que espera tras detectar goteo antes de volver a llenar

// -------- TAREAS FreeRTOS --------
#define STACK_CONTROL    4096
#define STACK_ACTUADORES 2048
#define STACK_DISPLAY    2048
#define STACK_MQTT       8192

#define PRIO_CONTROL    2   // Mayor prioridad: toma decisiones
#define PRIO_ACTUADORES 1
#define PRIO_DISPLAY    1
#define PRIO_MQTT       1

#define CORE_PRINCIPAL  1   // Control, actuadores y display
#define CORE_MQTT       0   // MQTT en core separado para no bloquear

// -------- COLA DE EVENTOS --------
#define QUEUE_SIZE      10  // Máximo de eventos pendientes de enviar por MQTT
