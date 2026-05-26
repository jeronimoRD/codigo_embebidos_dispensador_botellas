// =============================================================================
// planta_embotelladora.ino
// =============================================================================
// Sistema automático de llenado de vasos con banda transportadora.
//
// HARDWARE:
//   - ESP32 (dual-core)
//   - Motor DC de banda transportadora  → PIN_MOTOR_BANDA
//   - Bomba de agua                     → PIN_BOMBA
//   - Sensor IR de presencia            → PIN_IR
//   - Sensor de flujo YF-Sxx            → PIN_FLUJO
//   - Display LCD 16x2 I2C (addr 0x27)
//
// FLUJO DE ESTADOS:
//   BUSCANDO → (botella detectada) → LLENANDO
//   LLENANDO → (pulsos >= objetivo) → FINALIZADO
//   LLENANDO → (botella retirada)   → EMERGENCIA
//   FINALIZADO → (tiempo + botella salió) → BUSCANDO
//   EMERGENCIA → (botella vuelve)   → LLENANDO
//
// TAREAS FreeRTOS:
//   taskControl    [Core 1, Prio 2] — Máquina de estados principal
//   taskActuadores [Core 1, Prio 1] — Manejo de motor y bomba
//   taskDisplay    [Core 1, Prio 1] — Actualización del LCD
//   taskMQTT       [Core 0, Prio 1] — Envío de eventos al broker
//
// =============================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"   

// =============================================================================
// TIPOS Y ESTRUCTURAS
// =============================================================================

/**
 * @brief Estados posibles de la máquina de estados del proceso.
 */
enum EstadoProceso {
    BUSCANDO,    ///< Banda en marcha, esperando botella
    LLENANDO,    ///< Botella presente, bomba activa
    FINALIZADO,  ///< Llenado completo, banda expulsa la botella
    EMERGENCIA   ///< Botella retirada durante el llenado
};

/**
 * @brief Evento que se encola para enviar por MQTT y mostrar en Serial.
 */
struct LogEvent {
    char evento[20];    ///< Nombre corto del estado (ej. "LLENANDO")
    char detalles[50];  ///< Descripción legible del evento
    int  botellas;      ///< Contador de botellas al momento del evento
};

// =============================================================================
// VARIABLES GLOBALES
// =============================================================================

// -------- Estado del proceso --------
volatile EstadoProceso estadoActual = BUSCANDO;
int contadorBotellas = 0;

// -------- Sensor de flujo --------
// pulsosFlujo es escrita en la ISR y leída en taskControl.
// Se protege con secciones críticas al leer para evitar lecturas parciales
// en arquitecturas de 32 bits donde long no es atómico por hardware.
volatile long pulsosFlujo = 0;
volatile unsigned long ultimaInterrupcionFlujo = 0;

// -------- Banda: tiempo de activación forzada --------
// Permite mantener la banda encendida un tiempo fijo después del llenado,
// independientemente del estado, para asegurar que la botella salga.
volatile unsigned long bandaActivaHasta = 0;

// -------- Objetos de hardware --------
WiFiClient    espClient;
PubSubClient  mqttClient(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// -------- Cola de eventos para MQTT --------
QueueHandle_t logQueue;

// -------- Semáforo para proteger contadorBotellas --------
SemaphoreHandle_t semContador;

// =============================================================================
// INTERRUPCIÓN DE FLUJO
// =============================================================================

/**
 * @brief ISR del sensor de flujo. Cuenta pulsos con debounce por software.
 *
 * El sensor YF-Sxx genera ruido en los flancos. El debounce de MS_DEBOUNCE_FLUJO
 * descarta pulsos que llegan demasiado juntos (rebotes mecánicos).
 * Se marca IRAM_ATTR para que resida en RAM y no tenga latencia de caché.
 */
void IRAM_ATTR isrFlujo() {
    unsigned long ahora = millis();
    if (ahora - ultimaInterrupcionFlujo > MS_DEBOUNCE_FLUJO) {
        pulsosFlujo++;
        ultimaInterrupcionFlujo = ahora;
    }
}

// =============================================================================
// FUNCIONES AUXILIARES
// =============================================================================

/**
 * @brief Lee pulsosFlujo de forma segura desde cualquier tarea (no desde ISR).
 * @return Valor actual del contador de pulsos.
 */
long leerPulsos() {
    portDISABLE_INTERRUPTS();
    long val = pulsosFlujo;
    portENABLE_INTERRUPTS();
    return val;
}

/**
 * @brief Resetea el contador de pulsos de forma segura.
 */
void resetearPulsos() {
    portDISABLE_INTERRUPTS();
    pulsosFlujo = 0;
    portENABLE_INTERRUPTS();
}

/**
 * @brief Encola un evento de log para que taskMQTT lo envíe.
 *        Si la cola está llena, lo reporta por Serial sin bloquear.
 *
 * @param evento   Nombre corto del evento
 * @param detalles Descripción legible
 * @param botellas Contador actual de botellas
 */
void encolarLog(const char* evento, const char* detalles, int botellas) {
    LogEvent ev;
    strncpy(ev.evento,   evento,   sizeof(ev.evento)   - 1);
    strncpy(ev.detalles, detalles, sizeof(ev.detalles) - 1);
    ev.evento[sizeof(ev.evento) - 1]     = '\0';
    ev.detalles[sizeof(ev.detalles) - 1] = '\0';
    ev.botellas = botellas;

    if (xQueueSend(logQueue, &ev, 0) != pdTRUE) {
        Serial.println("[WARN] Cola de logs llena, evento descartado: " + String(evento));
    }
}

/**
 * @brief Convierte el estado actual a texto para el display.
 */
const char* estadoATexto(EstadoProceso estado) {
    switch (estado) {
        case BUSCANDO:   return "ESPERANDO  ";
        case LLENANDO:   return "LLENANDO   ";
        case FINALIZADO: return "TERMINADO  ";
        case EMERGENCIA: return "EMERGENCIA ";
        default:         return "???        ";
    }
}

// =============================================================================
// TAREA 1: CONTROL — Máquina de estados principal
// =============================================================================

/**
 * @brief Tarea de control principal. Implementa la máquina de estados del proceso.
 *
 * Prioridad alta (2) para que las decisiones de control no sean bloqueadas
 * por tareas de display o comunicación.
 *
 * @param pvParameters No utilizado.
 */
void taskControl(void* pvParameters) {
    bool reporteEnviado = false;

    for (;;) {
        bool hayBotella = (digitalRead(PIN_IR) == LOW);

        switch (estadoActual) {

            // -----------------------------------------------------------------
            case BUSCANDO: {
                if (!reporteEnviado) {
                    encolarLog("ESPERANDO", "Banda activa, buscando botella", contadorBotellas);
                    reporteEnviado = true;
                }
                if (hayBotella) {
                    resetearPulsos();
                    estadoActual   = LLENANDO;
                    reporteEnviado = false;
                }
                break;
            }

            // -----------------------------------------------------------------
            case LLENANDO: {
                if (!reporteEnviado) {
                    encolarLog("LLENANDO", "Botella detectada, bomba activa", contadorBotellas);
                    reporteEnviado = true;
                }
                if (!hayBotella) {
                    // Botella retirada antes de completar → emergencia
                    estadoActual   = EMERGENCIA;
                    reporteEnviado = false;
                } else if (leerPulsos() >= PULSOS_OBJETIVO) {
                    // Volumen objetivo alcanzado
                    estadoActual   = FINALIZADO;
                    reporteEnviado = false;
                }
                break;
            }

            // -----------------------------------------------------------------
            case FINALIZADO: {
                if (!reporteEnviado) {
                    // Incrementar contador con semáforo para acceso seguro
                    if (xSemaphoreTake(semContador, pdMS_TO_TICKS(10)) == pdTRUE) {
                        contadorBotellas++;
                        xSemaphoreGive(semContador);
                    }
                    encolarLog("FINALIZADO", "Llenado completo", contadorBotellas);

                    // Activar banda por tiempo fijo para expulsar la botella.
                    // taskActuadores leerá bandaActivaHasta y mantendrá la banda
                    // encendida sin importar el estado de la máquina.
                    bandaActivaHasta = millis() + MS_ESPERA_POST_LLENADO;

                    reporteEnviado = true;
                }

                // Esperar a que la banda haya tenido tiempo de mover la botella
                // y a que el sensor IR confirme que ya no hay botella.
                if (millis() >= bandaActivaHasta && !hayBotella) {
                    vTaskDelay(pdMS_TO_TICKS(MS_DEBOUNCE_IR)); // debounce final
                    estadoActual   = BUSCANDO;
                    reporteEnviado = false;
                }
                break;
            }

            // -----------------------------------------------------------------
            case EMERGENCIA: {
                if (!reporteEnviado) {
                    encolarLog("EMERGENCIA", "Botella retirada antes de completar", contadorBotellas);
                    reporteEnviado = true;
                }
                if (hayBotella) {
                    // La botella volvió → continuar llenando
                    estadoActual   = LLENANDO;
                    reporteEnviado = false;
                }
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =============================================================================
// TAREA 2: ACTUADORES — Control de motor y bomba
// =============================================================================

/**
 * @brief Tarea de actuadores. Traduce el estado actual a señales digitales.
 *
 * La lógica de la banda incluye un tiempo de activación forzada (bandaActivaHasta)
 * que permite expulsar la botella aunque el estado ya haya cambiado.
 *
 * | Estado     | Banda | Bomba |
 * |------------|-------|-------|
 * | BUSCANDO   |  ON   |  OFF  |
 * | LLENANDO   |  OFF  |  ON   |
 * | FINALIZADO |  ON*  |  OFF  |  (* por bandaActivaHasta)
 * | EMERGENCIA |  OFF  |  OFF  |
 *
 * @param pvParameters No utilizado.
 */
void taskActuadores(void* pvParameters) {
    for (;;) {
        EstadoProceso estado = estadoActual; // leer una sola vez por ciclo

        // La banda está activa si el estado la requiere O si hay tiempo forzado
        bool bandaActiva = (estado == BUSCANDO) ||
                           (millis() < bandaActivaHasta);

        // La bomba solo activa durante el llenado normal
        bool bombaActiva = (estado == LLENANDO);

        digitalWrite(PIN_MOTOR_BANDA, bandaActiva ? HIGH : LOW);
        digitalWrite(PIN_BOMBA,       bombaActiva ? HIGH : LOW);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =============================================================================
// TAREA 3: MQTT — Envío de eventos al broker
// =============================================================================

/**
 * @brief Tarea de comunicación MQTT. Mantiene la conexión y envía eventos.
 *
 * Corre en Core 0 para no interferir con el control en Core 1.
 * Si la conexión se pierde, reintenta cada 5 segundos.
 * Los mensajes se reciben de logQueue en formato JSON.
 *
 * @param pvParameters No utilizado.
 */
void taskMQTT(void* pvParameters) {
    LogEvent ev;

    for (;;) {
        // ---- Reconexión automática ----
        if (!mqttClient.connected()) {
            Serial.print("[MQTT] Conectando...");
            if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
                mqttClient.subscribe(MQTT_TOPIC_LOG);
                Serial.println(" OK");
            } else {
                Serial.printf(" Fallo (rc=%d). Reintentando en 5s.\n", mqttClient.state());
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        mqttClient.loop();

        // ---- Publicar eventos pendientes ----
        if (xQueueReceive(logQueue, &ev, 0) == pdTRUE) {
            char buffer[150];
            snprintf(buffer, sizeof(buffer),
                     "{\"evento\":\"%s\",\"detalles\":\"%s\",\"botellas\":%d}",
                     ev.evento, ev.detalles, ev.botellas);

            if (mqttClient.publish(MQTT_TOPIC_LOG, buffer)) {
                Serial.printf("[MQTT] Publicado: %s\n", buffer);
            } else {
                Serial.printf("[MQTT ERR] No se pudo publicar: %s\n", ev.evento);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// =============================================================================
// TAREA 4: DISPLAY — Actualización del LCD
// =============================================================================

/**
 * @brief Tarea de display. Actualiza el LCD 16x2 cada 400 ms.
 *
 * Línea 0: Estado actual del proceso
 * Línea 1: Contador de botellas y pulsos de flujo acumulados
 *
 * Ejemplo:
 *   EST: LLENANDO
 *   BOTE: 3  P: 187
 *
 * @param pvParameters No utilizado.
 */
void taskDisplay(void* pvParameters) {
    char linea1[17];
    char linea2[17];

    for (;;) {
        // Leer contador con semáforo
        int botellas = 0;
        if (xSemaphoreTake(semContador, pdMS_TO_TICKS(10)) == pdTRUE) {
            botellas = contadorBotellas;
            xSemaphoreGive(semContador);
        }

        snprintf(linea1, sizeof(linea1), "EST:%-11s", estadoATexto(estadoActual));
        snprintf(linea2, sizeof(linea2), "BOTE:%-2d P:%-5ld", botellas, leerPulsos());

        lcd.setCursor(0, 0); lcd.print(linea1);
        lcd.setCursor(0, 1); lcd.print(linea2);

        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Planta Embotelladora ===");

    // ---- Pines ----
    pinMode(PIN_MOTOR_BANDA, OUTPUT);
    pinMode(PIN_BOMBA,       OUTPUT);
    pinMode(PIN_IR,          INPUT_PULLUP);
    pinMode(PIN_FLUJO,       INPUT_PULLUP);

    // Estado seguro inicial: todo apagado
    digitalWrite(PIN_MOTOR_BANDA, LOW);
    digitalWrite(PIN_BOMBA, LOW);

    // ---- Interrupción de flujo ----
    attachInterrupt(digitalPinToInterrupt(PIN_FLUJO), isrFlujo, RISING);

    // ---- LCD ----
    lcd.init();
    lcd.backlight();
    lcd.print("Iniciando...");

    // ---- WiFi ----
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[WiFi] Conectando");
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Sin conexión. El sistema funciona sin MQTT.");
    }

    // ---- MQTT ----
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

    // ---- Recursos FreeRTOS ----
    logQueue   = xQueueCreate(QUEUE_SIZE, sizeof(LogEvent));
    semContador = xSemaphoreCreateMutex();

    if (logQueue == NULL || semContador == NULL) {
        Serial.println("[ERROR] No se pudieron crear recursos FreeRTOS. Reiniciando...");
        ESP.restart();
    }

    // ---- Tareas ----
    xTaskCreatePinnedToCore(taskControl,    "Control",    STACK_CONTROL,    NULL, PRIO_CONTROL,    NULL, CORE_PRINCIPAL);
    xTaskCreatePinnedToCore(taskActuadores, "Actuadores", STACK_ACTUADORES, NULL, PRIO_ACTUADORES, NULL, CORE_PRINCIPAL);
    xTaskCreatePinnedToCore(taskDisplay,    "Display",    STACK_DISPLAY,    NULL, PRIO_DISPLAY,    NULL, CORE_PRINCIPAL);
    xTaskCreatePinnedToCore(taskMQTT,       "MQTT",       STACK_MQTT,       NULL, PRIO_MQTT,       NULL, CORE_MQTT);

    Serial.println("[OK] Tareas iniciadas. Sistema listo.");
}

// loop() queda vacío: FreeRTOS toma el control
void loop() {}
