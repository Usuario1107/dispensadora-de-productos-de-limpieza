# Especificación Técnica de Asignación e Interconexión de Pines (ESP32)

Este documento establece la arquitectura de conexión e integración de hardware para el microcontrolador ESP32 (38 pines) en el sistema dispensador de detergentes. La configuración garantiza el aislamiento eléctrico, la estabilidad del firmware durante el proceso de arranque (*boot*) y la inmunidad a interferencias electromagnéticas.

---

## 1. Sistema de Pago (Monedero Multicoin)

| Función / Periférico | Pin GPIO | Modo de Configuración | Destino / Interconexión de Hardware | Criterio de Diseño Electrónico |
| :--- | :--- | :--- | :--- | :--- |
| **Lectura de Pulsos (`COIN`)** | **GPIO 18** | `INPUT` | Salida de señal del monedero (Colector abierto) | Pin asignado a interrupción por hardware (`ISR`). Exento de restricciones de arranque. |
| **Control de Bloqueo (`SET`)** | **GPIO 19** | `OUTPUT` | Transistor / Etapa de control de inhibición del monedero | Conmuta el estado lógico para permitir o rechazar el ingreso de monedas según la FSM. |

---

## 2. Interfaz Visual (Pantalla LCD 20x4 I2C)

| Función / Periférico | Pin GPIO | Modo de Configuración | Destino / Interconexión de Hardware | Criterio de Diseño Electrónico |
| :--- | :--- | :--- | :--- | :--- |
| **Línea de Datos (`SDA`)** | **GPIO 21** | Bus I2C Hardware | Pin SDA del adaptador PCF8574 acoplado al LCD | Utiliza los pines nativos de la librería `Wire.h`. |
| **Línea de Reloj (`SCL`)** | **GPIO 22** | Bus I2C Hardware | Pin SCL del adaptador PCF8574 acoplado al LCD | Bus de comunicación estándar de alta velocidad. |

---

## 3. Control de Potencia (Salidas hacia PLC)

| Función / Periférico | Pin GPIO | Modo de Configuración | Destino / Interconexión de Hardware | Criterio de Diseño Electrónico |
| :--- | :--- | :--- | :--- | :--- |
| **Activación Bomba 1** | **GPIO 17** | `OUTPUT` | Entrada Canal 1 del módulo optoacoplador | Aislamiento galvánico directo previo al PLC. Estado inicial en estado bajo (`LOW`). |
| **Activación Bomba 2** | **GPIO 16** | `OUTPUT` | Entrada Canal 2 del módulo optoacoplador | Aislamiento galvánico directo previo al PLC. Estado inicial en estado bajo (`LOW`). |
| **Activación Bomba 3** | **GPIO 4** | `OUTPUT` | Entrada Canal 3 del módulo optoacoplador | Aislamiento galvánico directo previo al PLC. Estado inicial en estado bajo (`LOW`). |

---

## 4. Panel de Navegación del Menú (LCD)

| Función / Periférico | Pin GPIO | Modo de Configuración | Destino / Interconexión de Hardware | Criterio de Diseño Electrónico |
| :--- | :--- | :--- | :--- | :--- |
| **Botón 1 (`[1]`)** | **GPIO 26** | `INPUT_PULLUP` | Pulsador del panel frontal conectado a `GND` | Lectura por flanco de bajada (*falling edge*). |
| **Botón 2 (`[2]`)** | **GPIO 27** | `INPUT_PULLUP` | Pulsador del panel frontal conectado a `GND` | Lectura por flanco de bajada (*falling edge*). |
| **Botón 3 (`[3]`)** | **GPIO 14** | `INPUT_PULLUP` | Pulsador del panel frontal conectado a `GND` | Lectura por flanco de bajada (*falling edge*). |
| **Botón 4 (`[4]`)** | **GPIO 13** | `INPUT_PULLUP` | Pulsador del panel frontal conectado a `GND` | Botón multifunción (Atrás / Cancelar / Confirmar). |

---

## 5. Panel de Dispensado (Pulsadores de Boquillas)

| Función / Periférico | Pin GPIO | Modo de Configuración | Destino / Interconexión de Hardware | Criterio de Diseño Electrónico |
| :--- | :--- | :--- | :--- | :--- |
| **Pulsador Boquilla 1** | **GPIO 32** | `INPUT_PULLUP` | Pulsador de la boquilla 1 conectado a `GND` | Se habilita dinámicamente solo tras confirmar la validación del pago. |
| **Pulsador Boquilla 2** | **GPIO 33** | `INPUT_PULLUP` | Pulsador de la boquilla 2 conectado a `GND` | Se habilita dinámicamente solo tras confirmar la validación del pago. |
| **Pulsador Boquilla 3** | **GPIO 25** | `INPUT_PULLUP` | Pulsador de la boquilla 3 conectado a `GND` | Se habilita dinámicamente solo tras confirmar la validación del pago. |

---

## 6. Monitoreo de Caudal (Sensores de Flujo YF-S201)

| Función / Periférico | Pin GPIO | Etiqueta Placa | Modo de Configuración | Destino / Interconexión de Hardware | Criterio de Diseño Electrónico |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Sensor Flujo Bomba 1** | **GPIO 35** | `D35` / `35` | `INPUT` + `ISR` | Salida del sensor Hall (Línea 1) | Pin de entrada pura. Requiere resistencia de *pull-up* externa a 3.3V e interrupción por hardware. |
| **Sensor Flujo Bomba 2** | **GPIO 34** | `D34` / `34` | `INPUT` + `ISR` | Salida del sensor Hall (Línea 2) | Pin de entrada pura. Requiere resistencia de *pull-up* externa a 3.3V e interrupción por hardware. |
| **Sensor Flujo Bomba 3** | **GPIO 36** | **`VP`** / `36` | `INPUT` + `ISR` | Salida del sensor Hall (Línea 3) | Pin de entrada pura. Requiere resistencia de *pull-up* externa a 3.3V e interrupción por hardware. |

---

## 7. Directrices Eléctricas y de Inmunidad a Ruido

* **Pines de Entrada Pura (GPI 34, 35 y 36/VP):** Estos pines carecen físicamente de resistencias internas de elevación. Es **estrictamente obligatorio colocar una resistencia externa de 10 kΩ conectada a 3.3V** en la línea de señal de cada sensor de flujo para evitar lecturas flotantes o falsos pulsos.
* **Acondicionamiento de Voltaje (5V a 3.3V):** Si los sensores de flujo se alimentan con 5V, se debe integrar un **divisor de tensión** (ej. resistencia de 1 kΩ en serie y 2 kΩ a GND) o un módulo convertidor de nivel lógico bidireccional. Esto garantiza que los pulsos enviados al ESP32 nunca superen el límite máximo tolerable de 3.3V, evitando daños permanentes en el microcontrolador.
* **Cableado Extenso (> 0.5 metros):** Para minimizar el acoplamiento magnético y el ruido electromagnético en los cables de los pulsadores y sensores, se debe incorporar en el PCB un condensador cerámico de 100 nF instalado en paralelo entre el pin de entrada y `GND`. Para los botones, usar resistencias *pull-up* externas de 4.7 kΩ a 10 kΩ refuerza la inmunidad al ruido.
* **Exclusión de Pines Críticos:** Se omite categóricamente el uso de los GPIO 6 al 11 (asignados al bus interno SPI de la memoria Flash) y los pines de estado de arranque (*strapping pins*) GPIO 0, 2, 12 y 15 para prevenir bloqueos de hardware durante el encendido.
