#pragma once

// =============================================================================
// config.example.h — Plantilla pública de configuración
// =============================================================================
// Copia este archivo como config.h y completa tus datos reales.
// El archivo config.h está en .gitignore y NUNCA se sube al repositorio.
// =============================================================================

// -------- RED WiFi --------
#define WIFI_SSID       "TuRedWiFi"
#define WIFI_PASSWORD   "TuContrasena"

// -------- BROKER MQTT --------
#define MQTT_SERVER     "192.168.X.X"
#define MQTT_PORT       1883
#define MQTT_CLIENT_ID  "ESP32_Planta"
#define MQTT_USER       "iot_device"
#define MQTT_PASSWORD   "TuPasswordMQTT"
#define MQTT_TOPIC_LOG  "planta/logs"

// -------- PINES --------
#define PIN_MOTOR_BANDA  5
#define PIN_BOMBA        18
#define PIN_IR           19
#define PIN_FLUJO         4

// -------- PARÁMETROS DE LLENADO --------
#define PULSOS_OBJETIVO  294L

// -------- PARÁMETROS DE TIEMPO --------
#define MS_ESPERA_POST_LLENADO   3000
#define MS_DEBOUNCE_IR            500
#define MS_DEBOUNCE_FLUJO           2

// -------- TAREAS FreeRTOS --------
#define STACK_CONTROL    4096
#define STACK_ACTUADORES 2048
#define STACK_DISPLAY    2048
#define STACK_MQTT       8192

#define PRIO_CONTROL    2
#define PRIO_ACTUADORES 1
#define PRIO_DISPLAY    1
#define PRIO_MQTT       1

#define CORE_PRINCIPAL  1
#define CORE_MQTT       0

#define QUEUE_SIZE      10
