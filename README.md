# 🏭 Planta Embotelladora Automática — ESP32

Sistema de llenado automático de vasos con banda transportadora, controlado por un ESP32 usando FreeRTOS y comunicación MQTT.

## 📷 Prototipo

> Banda transportadora 3D impresa con motor DC, bomba de agua, sensor IR y sensor de flujo.

## ⚙️ Hardware

| Componente            | Pin ESP32 |
|-----------------------|-----------|
| Motor banda (IN1)     | GPIO 5    |
| Bomba de agua         | GPIO 18   |
| Sensor IR presencia   | GPIO 19   |
| Sensor de flujo YF-S  | GPIO 4    |
| LCD 16x2 I2C (SDA)    | GPIO 21   |
| LCD 16x2 I2C (SCL)    | GPIO 22   |

## 🔄 Diagrama de estados

```
         [botella detectada]
BUSCANDO ────────────────────► LLENANDO
   ▲                              │  │
   │         [pulsos >= objetivo] │  │ [botella retirada]
   │    FINALIZADO ◄──────────────┘  │
   │       │                         ▼
   └───────┘ [botella salió]    EMERGENCIA
   [BUSCANDO]                        │
                    [botella vuelve] │
                    LLENANDO ◄───────┘
```

## 📋 Tareas FreeRTOS

| Tarea        | Core | Prioridad | Función                          |
|--------------|------|-----------|----------------------------------|
| taskControl  | 1    | 2         | Máquina de estados principal     |
| taskActuadores | 1  | 1         | Control de motor y bomba         |
| taskDisplay  | 1    | 1         | Actualización del LCD            |
| taskMQTT     | 0    | 1         | Envío de eventos al broker       |

## 🚀 Instalación

### 1. Clonar el repositorio
```bash
git clone https://github.com/tu-usuario/planta-embotelladora.git
cd planta-embotelladora
```

### 2. Configurar credenciales
```bash
cp src/config.example.h src/config.h
# Edita src/config.h con tu red WiFi y broker MQTT
```

### 3. Instalar librerías (Arduino IDE)
- `PubSubClient` by Nick O'Leary
- `LiquidCrystal_I2C` by Frank de Brabander
- `ESP32` board package (Espressif)

### 4. Subir el sketch
Abre `src/planta_embotelladora.ino` en Arduino IDE, selecciona tu placa ESP32 y sube.

## 📡 Mensajes MQTT

Todos los eventos se publican en el topic `planta/logs` en formato JSON:

```json
{"evento":"LLENANDO","detalles":"Botella detectada, bomba activa","botellas":3}
```

### Eventos posibles

| evento      | Descripción                              |
|-------------|------------------------------------------|
| `ESPERANDO` | Banda activa, buscando botella           |
| `LLENANDO`  | Botella presente, bomba activa           |
| `FINALIZADO`| Llenado completo                         |
| `EMERGENCIA`| Botella retirada antes de completar      |

## 🔧 Ajuste de volumen

El volumen se controla con `PULSOS_OBJETIVO` en `config.h`.

Para calibrar:
1. Coloca un vaso lleno de agua conocida (ej. 200 ml)
2. Observa el Serial Monitor y anota los pulsos al alcanzar ese volumen
3. Ajusta `PULSOS_OBJETIVO` con ese valor

## 📁 Estructura del proyecto

```
planta_embotelladora/
├── src/
│   ├── planta_embotelladora.ino   # Código principal
│   ├── config.h                   # ⚠️ Credenciales (en .gitignore)
│   └── config.example.h           # Plantilla pública de configuración
├── .gitignore
└── README.md
```

## 👤 Autor

Tu nombre — Universidad / Materia — 2025
