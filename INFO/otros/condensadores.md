#  Selección de Componentes THT (Inserción)

Para facilitar el ensamblaje manual y el mantenimiento de la tarjeta de control de la máquina expendedora, todos los componentes pasivos (condensadores y resistencias) han sido especificados en formato **THT (Through-Hole Technology / Componentes de Inserción)**.

---

## 1. Selección y Valores de Condensadores THT

*   **Condensadores Cerámicos de Desacoplo (Tipo Lenteja / Disco):**
    *   **Valor estándar:** `100 nF` (0.1 µF / Código impreso común: `104`).
    *   **Voltaje de trabajo:** Mínimo **50V**.
    *   **Aplicación:** Se colocan lo más cerca posible de los pines de alimentación del ESP32, a la salida del regulador LM2596 y en las entradas digitales para filtrar ruido de alta frecuencia.
*   **Condensadores Electrolíticos (Tipo Radial / Cilíndricos):**
    *   **Valores estándar:** `100 µF` a `220 µF`.
    *   **Voltaje de trabajo:** Mínimo **25V o 35V**.
    *   **Aplicación:** Se usan en la entrada de 12V y en la salida de 5V del regulador para actuar como reservas de energía y estabilizar el rizado de corriente. *Nota al soldar:* Respetar estrictamente la polaridad (la patita larga o marcada con signo negativo (-) en la franja blanca va hacia la tierra/GND).

---

## 2. Guía de Ubicación de Componentes THT en la PCB

| Ubicación en la PCB | Tipo de Componente THT | Valor Recomendado | Propósito Técnico |
| :--- | :--- | :--- | :--- |
| **Entrada del Regulador (12V)** | Electrolítico Radial + Cerámico Lenteja | `100 µF` (25V) + `100 nF` (50V) | Filtra interferencias de la fuente principal antes del regulador. |
| **Salida del Regulador (5V - LM2596)** | Electrolítico Radial + Cerámico Lenteja | `100 µF` (25V) + `100 nF` (50V) | Estabiliza los 5V que van hacia la tarjeta de control. |
| **Alimentación ESP32 (`VIN` o `5V`)** | Cerámico Radial (Lenteja) | `100 nF` (50V) | Mitiga caídas de tensión instantáneas durante picos de Wi-Fi/Bluetooth. |
| **Líneas de Entrada (Optoacopladores)** | Cerámico Radial (Lenteja) | `100 nF` (50V) | En paralelo entre la señal del opto y GND para apastar ruido inducido por cables largos. |
| **Resistencias de Pull-Up (Flujo)** | Resistencia de Carbón / Película Metálica | `10 kΩ` (1/4 Watt) | Obligatorias en los pines 34 y 35 del ESP32 para los sensores de caudal. |
