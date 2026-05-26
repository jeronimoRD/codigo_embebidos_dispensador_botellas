#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>

// -------- CONFIGURACIÓN RED --------
const char* ssid = "Jero";
const char* password = "30329385";
const char* mqtt_server = "10.113.26.62"; 

// -------- PINES --------
#define PIN_MOTOR_BANDA 5
#define PIN_BOMBA 18
#define PIN_IR 19
#define PIN_FLUJO 4

// -------- OBJETOS --------
WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);
QueueHandle_t logQueue;

// -------- VARIABLES GLOBALES --------
enum EstadoProceso { BUSCANDO, LLENANDO, FINALIZADO, EXPULSANDO, EMERGENCIA };
EstadoProceso estadoActual = BUSCANDO;

int contadorBotellas = 0;
const long PULSOS_OBJETIVO = 294;

struct LogEvent {
    char evento[20];
    char detalles[50];
    int botellas;
};

// Interrupción para conteo de flujo
volatile long pulsosFlujo = 0;
volatile unsigned long ultimaInterrupcion = 0;

void IRAM_ATTR conteoFlujo() {
    unsigned long tiempoActual = millis();
    if (tiempoActual - ultimaInterrupcion > 2) { 
        pulsosFlujo++;
        ultimaInterrupcion = tiempoActual;
    }
}

// -------- TAREA 1: CONTROL --------
void taskControl(void *pvParameters) {
    bool reporteEnviado = false; 

    for (;;) {
        bool hayBotella = (digitalRead(PIN_IR) == LOW);

        switch (estadoActual) {
            case BUSCANDO: {
                if (!reporteEnviado) {
                    LogEvent ev = {"ESPERANDO", "Buscando botella", contadorBotellas};
                    xQueueSend(logQueue, &ev, 0);
                    reporteEnviado = true;
                }
                if (hayBotella) {
                    pulsosFlujo = 0;
                    estadoActual = LLENANDO;  
                    reporteEnviado = false;
                }
                break;
            }

            case LLENANDO: {
                if (!reporteEnviado) {
                    LogEvent ev = {"LLENANDO", "Botella detectada", contadorBotellas};
                    xQueueSend(logQueue, &ev, 0);
                    reporteEnviado = true;
                }
                if (!hayBotella) {
                    estadoActual = EMERGENCIA;
                    reporteEnviado = false;
                } else if (pulsosFlujo >= PULSOS_OBJETIVO) {
                    estadoActual = FINALIZADO;
                    reporteEnviado = false;
                }
                break;
            }

            case FINALIZADO: {
                if (!reporteEnviado) {
                    contadorBotellas++;
                    LogEvent ev = {"FINALIZADO", "Llenado completo", contadorBotellas};
                    xQueueSend(logQueue, &ev, 0);
                    reporteEnviado = true;
                }
                // Esperar 3 segundos antes de expulsar
                vTaskDelay(pdMS_TO_TICKS(3000));
                
                // Cambiar a EXPULSANDO para que taskActuadores encienda la banda
                estadoActual = EXPULSANDO;
                
                // Esperar a que la botella salga del sensor IR
                while (digitalRead(PIN_IR) == LOW) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                
                // Debounce: esperar un poco tras salir la botella
                vTaskDelay(pdMS_TO_TICKS(500));
                
                estadoActual = BUSCANDO;
                reporteEnviado = false;
                break;
            }

            case EXPULSANDO: {
                // taskActuadores se encarga de mover la banda
                // Esta tarea solo espera, el flujo lo maneja FINALIZADO
                break;
            }

            case EMERGENCIA: {
                if (!reporteEnviado) {
                    LogEvent ev = {"EMERGENCIA", "Botella retirada!", contadorBotellas};
                    xQueueSend(logQueue, &ev, 0);
                    reporteEnviado = true;
                }
                if (hayBotella) {
                    estadoActual = LLENANDO;
                    reporteEnviado = false;
                }
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// -------- TAREA 2: ACTUADORES --------
void taskActuadores(void *pvParameters) {
    for (;;) {
        if (estadoActual == BUSCANDO) {
            digitalWrite(PIN_MOTOR_BANDA, HIGH);
            digitalWrite(PIN_BOMBA, LOW);
        } else if (estadoActual == LLENANDO) {
            digitalWrite(PIN_MOTOR_BANDA, LOW);
            digitalWrite(PIN_BOMBA, HIGH);
        } else if (estadoActual == EXPULSANDO) {
            digitalWrite(PIN_MOTOR_BANDA, HIGH);
            digitalWrite(PIN_BOMBA, LOW);
        } else {
            // FINALIZADO y EMERGENCIA: todo apagado
            digitalWrite(PIN_MOTOR_BANDA, LOW);
            digitalWrite(PIN_BOMBA, LOW);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// -------- TAREA 3: MQTT --------
void taskMQTT(void *pvParameters) {
    LogEvent ev;
    for (;;) {
        if (!client.connected()) {
            Serial.print("Intentando conectar MQTT...");
            if (client.connect("ESP32_Planta", "iot_device", "SecurePass123!")) {
                client.subscribe("planta/logs");
                Serial.println(" ¡CONECTADO!");
            } else {
                Serial.println(" Falló. Reintento en 5s.");
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
        }
        
        client.loop();

        if (xQueueReceive(logQueue, &ev, 0)) {
            char buffer[150];
            snprintf(buffer, 150, "{\"evento\":\"%s\",\"detalles\":\"%s\",\"botellas\":%d}", 
                     ev.evento, ev.detalles, ev.botellas);
            
            if (client.publish("planta/logs", buffer)) {
                Serial.print("[MQTT OK] Enviado estado: ");
                Serial.println(ev.evento);
            } else {
                Serial.print("[MQTT ERROR] No se pudo enviar: ");
                Serial.println(ev.evento);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// -------- TAREA 4: DISPLAY --------
void taskDisplay(void *pvParameters) {
    for (;;) {
        lcd.setCursor(0, 0);
        lcd.print("EST: ");
        if (estadoActual == BUSCANDO)   lcd.print("ESPERANDO  ");
        if (estadoActual == LLENANDO)   lcd.print("LLENANDO   ");
        if (estadoActual == FINALIZADO) lcd.print("TERMINADO  ");
        if (estadoActual == EXPULSANDO) lcd.print("EXPULSANDO ");
        if (estadoActual == EMERGENCIA) lcd.print("EMERGENCIA ");

        lcd.setCursor(0, 1);
        lcd.print("BOTE: "); lcd.print(contadorBotellas);
        lcd.print(" P:"); lcd.print(pulsosFlujo);
        
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_MOTOR_BANDA, OUTPUT);
    pinMode(PIN_BOMBA, OUTPUT);
    pinMode(PIN_IR, INPUT_PULLUP);
    pinMode(PIN_FLUJO, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(PIN_FLUJO), conteoFlujo, RISING);

    lcd.init();
    lcd.backlight();
    lcd.print("Iniciando...");

    WiFi.begin(ssid, password);
    Serial.print("Conectando a WiFi...");
    while (WiFi.status() != WL_CONNECTED) { 
        delay(500); 
        Serial.print(".");
    }
    Serial.println("\nWiFi Conectado!");
    
    client.setServer(mqtt_server, 1883);
    logQueue = xQueueCreate(10, sizeof(LogEvent));

    xTaskCreatePinnedToCore(taskControl,    "Ctrl", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(taskActuadores, "Act",  2048, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(taskDisplay,    "LCD",  2048, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(taskMQTT,       "MQTT", 8192, NULL, 1, NULL, 0);
}

void loop() {}