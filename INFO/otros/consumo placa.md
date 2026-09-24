# Análisis de Consumo Energético - Placa de Control (12V DC)

El Documento detalla el presupuesto energético (Power Budget) de la tarjeta electrónica de control para la máquina expendedora automatizada. Este análisis contempla **únicamente la fuente de 12V DC** destinada a la lógica de control, excluyendo la alimentación del PLC (24V) y el circuito de potencia de las bombas (12V 10A).

Dimensionar correctamente la fuente de alimentación primaria, garantizando estabilidad en el sistema ante picos de consumo generados por transmisiones inalámbricas (Wi-Fi/Bluetooth) y actuadores electromecánicos (monedero).

---

## 1. Criterios de Evaluación

Los valores expresados a continuación representan el amperaje extraído directamente de la línea principal de **12V DC**. (Para los componentes de 5V y 3.3V, el cálculo ya incluye la conversión a través del módulo Step-Down, asumiendo una eficiencia típica del 85%).

- **Estado Nominal (Reposo / Standby):** El sistema se encuentra encendido, la pantalla HMI está iluminada mostrando el menú principal, el ESP32 está a la espera de instrucciones (sin transmitir datos por red) y el monedero está listo para recibir monedas.
- **Estado Máximo (Pico / Carga Plena):** Es el peor escenario posible. Ocurre cuando el ESP32 transmite datos por Wi-Fi y Bluetooth simultáneamente, la pantalla HMI actualiza gráficos, el usuario presiona botones, y el monedero acciona su solenoide interno para validar una moneda, todo en el mismo instante.

---

## 2. Tabla de Consumo por Componente

| Componente / Módulo                                     | Consumo Nominal (Reposo) | Consumo Máximo (Pico) | Descripción del Estado Máximo                                                                             |
| :------------------------------------------------------ | :----------------------- | :-------------------- | :-------------------------------------------------------------------------------------------------------- |
| **Microcontrolador ESP32** _(Vía Step-Down)_            | 40 mA                    | 200 mA                | Transmisión activa de datos por antena Wi-Fi y Bluetooth simultáneamente.                                 |
| **HMI (Arduino Uno + Pantalla 2.4")** _(Vía Step-Down)_ | 120 mA                   | 160 mA                | Actualización rápida de gráficos en pantalla, retroiluminación al 100% y procesamiento de toques (Touch). |
| **Monedero Multimonedas** _(Directo a 12V)_             | 50 mA                    | 350 mA                | Activación de la bobina electromagnética (solenoide) para aceptar o rechazar una moneda física.           |
| **Periféricos y Optoacopladores** _(Señales a 12V)_     | 20 mA                    | 100 mA                | Todos los LEDs de los optoacopladores encendidos, botones pulsados y sensores de flujo enviando pulsos.   |
| **Módulo Step-Down** _(Pérdidas térmicas)_              | 20 mA                    | 40 mA                 | Energía disipada en forma de calor por el regulador al bajar el voltaje de 12V a 5V bajo carga.           |

---

## 3. Totales y Dimensionamiento del Sistema

Sumando los valores de la tabla anterior, se obtiene el consumo global de la tarjeta de control sobre la línea de 12V:

- **Total en Estado Nominal (Reposo):** `~ 250 mA (0.25 Amperios)`
- **Total en Estado Máximo (Pico):** `~ 950 mA (0.95 Amperios)`

### Conclusión Técnica y Recomendación de Hardware

Se observa que, durante el 95% del tiempo operativo de la máquina, la tarjeta de control demandará apenas un cuarto de amperio (0.25 A). Sin embargo, el diseño electrónico debe garantizar que la fuente sea capaz de suministrar el requerimiento pico de casi 1 Amperio sin sufrir caídas de tensión, las cuales provocarían el reinicio (reset) del microcontrolador ESP32.

**Especificación de la Fuente Requerida:**
Para garantizar una operación industrial ininterrumpida (24/7) y evitar fatiga térmica en los componentes de alimentación, se aplica un margen de seguridad del 200% sobre el consumo pico.

- **Fuente Sugerida:** Fuente de alimentación conmutada de **12V DC a 3 Amperios (36 Watts)**.
- **Justificación:** Una fuente de 3A trabajará a menos del 35% de su capacidad total durante los picos máximos. Esto asegura que la fuente opere completamente fría, maximizando su vida útil y entregando una corriente libre de rizado (ripple) para el correcto funcionamiento de los sensores y la comunicación serial UART.
