# Documentación Técnica de Hardware: Asignación de Pines (Pinout)

El presente documento detalla la asignación de pines del microcontrolador ESP32 (versión DevKit de 38 pines) para el sistema de control de la máquina expendedora. Este diseño está optimizado para entornos industriales, asegurando el aislamiento eléctrico de las señales.

## 1. Glosario y Criterios de Diseño (Pull-Up y Niveles Lógicos)

Para el diseño de la placa de circuito impreso (PCB) y la programación, se han establecido los siguientes criterios:

*   **Pull-Up (Resistencia de elevación):** Es una configuración donde el pin se mantiene conectado al voltaje positivo (3.3V) mediante una resistencia. El sistema lee un estado "Alto" por defecto, y cuando se activa el botón o sensor, la señal cae a cero ("Bajo"). La mayoría de las entradas de este sistema utilizan esta lógica para evitar lecturas falsas por ruido eléctrico.
*   **Pull-Down (Resistencia de caída):** Mantiene el pin conectado a tierra (0V) por defecto.
*   **HIGH (Alto):** Señal con presencia de voltaje (3.3V en el caso del ESP32).
*   **LOW (Bajo):** Señal sin voltaje o conectada a Tierra (GND / 0V).
*   **Optoacoplador / Optoaislado:** Componente que transmite la señal eléctrica mediante luz interna, aislando los voltajes altos (12V) del voltaje delicado del microcontrolador (3.3V). Protege al equipo de cortocircuitos y ruido electromagnético.
*   **Deep Sleep (Suspensión profunda):** Estado de máximo ahorro de energía del microcontrolador.
*   **Boot / Bootear:** Proceso de encendido y arranque inicial del sistema.

---

## 2. Tablas de Asignación por Módulo

A continuación, se detalla la conexión física de cada periférico al ESP32.

### A. Sistema de Cobro (Monedero)
El monedero opera con voltajes superiores al microcontrolador, por lo que sus señales están aisladas. Se bloquea enviando una señal a Tierra (LOW).

| Pin ESP32 | Función | Aislamiento (Hardware PCB) | Configuración (Software) |
| :--- | :--- | :--- | :--- |
| **GPIO 18** | Recepción de moneda (`COIN`) | **Sí (Optoacoplador).** Aisla la señal de 12V y cuenta con filtro de ruido. | Entrada con Pull-Up interno (`INPUT_PULLUP`). |
| **GPIO 19** | Bloqueo de monedero (`SET`) | **Sí (Optoacoplador).** La salida se activa en **LOW (Bajo)** para bloquear el dispositivo. | Salida (`OUTPUT`). |

### B. Interfaz Hombre-Máquina (HMI - Pantalla y Arduino Uno)
Comunicación en serie (UART2) con la pantalla táctil ubicada a 1 metro de distancia.

| Pin ESP32 | Función | Aislamiento (Hardware PCB) | Configuración (Software) |
| :--- | :--- | :--- | :--- |
| **GPIO 21** | Recibe datos del HMI (`RX2`) | **No.** Usa divisor de tensión (Resistencias) para bajar de 5V a 3.3V. | Comunicación Serial 2 (`Serial2.begin`). |
| **GPIO 22** | Envía datos al HMI (`TX2`) | **No.** Conexión directa. El HMI lee los 3.3V sin problema. | Comunicación Serial 2 (`Serial2.begin`). |

### C. Control de Bombas (Hacia PLC)
Señales de salida hacia el Controlador Lógico Programable (PLC). El PLC requiere una señal de Tierra (LOW / Bajo) para activar cada bomba.

| Pin ESP32 | Función | Aislamiento (Hardware PCB) | Configuración (Software) |
| :--- | :--- | :--- | :--- |
| **GPIO 17** | Activar Bomba 1 | **Sí (Optoacoplador).** Al activarse, conecta la entrada del PLC a Tierra (LOW). | Salida (`OUTPUT`).
| **GPIO 16** | Activar Bomba 2 | **Sí (Optoacoplador).** Al activarse, conecta la entrada del PLC a Tierra (LOW). | Salida (`OUTPUT`).
| **GPIO 4** | Activar Bomba 3 | **Sí (Optoacoplador).** Al activarse, conecta la entrada del PLC a Tierra (LOW). | Salida (`OUTPUT`).

*Nota de hardware:* Se sugiere una resistencia Pull-Down (10k a Tierra) en la PCB antes del optoacoplador para evitar que las bombas se enciendan por accidente durante el arranque (Boot).

### D. Botones de Selección (Dispensado)
Botones físicos accionados por el usuario. Operan a 12V en un cableado de 1 metro de longitud.

| Pin ESP32 | Función | Aislamiento (Hardware PCB) | Configuración (Software) |
| :--- | :--- | :--- | :--- |
| **GPIO 32** | Botón Opción 1 | **Sí (Optoacoplador).** Recibe la señal de 12V del botón. | Entrada con Pull-Up interno (`INPUT_PULLUP`). |
| **GPIO 33** | Botón Opción 2 | **Sí (Optoacoplador).** Recibe la señal de 12V del botón. | Entrada con Pull-Up interno (`INPUT_PULLUP`). |
| **GPIO 25** | Botón Opción 3 | **Sí (Optoacoplador).** Recibe la señal de 12V del botón. | Entrada con Pull-Up interno (`INPUT_PULLUP`). |

### E. Sensores de Nivel (Flotadores de contacto)
Sensores que detectan si los contenedores de líquido están vacíos no tinen optoacopaldor sobre filtro CR cermaico con 10 k y seri 220 ohms.

| Pin ESP32 | Función | Aislamiento (Hardware PCB) | Configuración (Software) |
| :--- | :--- | :--- | :--- |
| **GPIO 26** | Sensor Nivel Tanque 1 | **No.** pull up externo directo con filtro RC. | Entrada con Pull-Up interno (`INPUT_PULLUP`). |
| **GPIO 27** | Sensor Nivel Tanque 2 | **No.** pull up externo directo con filtro RC. | Entrada con Pull-Up interno (`INPUT_PULLUP`). |
| **GPIO 14** | Sensor Nivel Tanque 3 | **No.** pull up externo directo con filtro RC. | Entrada con Pull-Up interno (`INPUT_PULLUP`). |

### F. Sensores de Flujo (Caudalímetros)
Sensores que miden la cantidad de líquido dispensado. 

| Pin ESP32 | Función | Aislamiento (Hardware PCB) | Configuración (Software) |
| :--- | :--- | :--- | :--- |
| **GPIO 35** | Sensor Flujo Bomba 1 | Según voltaje del sensor. **Pull-Up EXTERNA (4.7k)** | Entrada estándar (`INPUT`). |
| **GPIO 34** | Sensor Flujo Bomba 2 | Según voltaje del sensor. **Pull-Up EXTERNA (4.7k).** | Entrada estándar (`INPUT`). |

*Nota:* Estos pines carecen de la capacidad física interna para realizar el Pull-Up, por lo que la resistencia física soldada en la placa es mandatoria para su funcionamiento.

### G. Control del Sistema y Energía

| Pin ESP32 | Función | Aislamiento (Hardware PCB) | Configuración (Software) |
| :--- | :--- | :--- | :--- |
| **GPIO 13** | Botón Despertar (Wake Up) | **Sí (Optoacoplador).** Activa el equipo desde la suspensión profunda (Deep Sleep). | Entrada con Pull-Up interno (`INPUT_PULLUP`). |
| **GND** | Tierra / Masa | Todas las tierras de las fuentes (12V y 5V), del Arduino HMI y del PLC deben estar unidas. | N/A |
| **VIN / 5V**| Alimentación Principal | Conectado al regulador Step-Down de 5V DC . | N/A |
| **3.3V**| Auxliar externo salida. | N/A |

---
*Todas la entradas  son pull up*
