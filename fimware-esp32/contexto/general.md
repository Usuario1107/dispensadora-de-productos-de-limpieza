#  Documentación General: Dispensador Automático de Detergentes

## 📌 1. Descripción del Proyecto
Este proyecto consiste en el desarrollo del firmware para una máquina expendedora automática de líquidos (3 tipos de detergentes). El sistema permite al usuario consultar precios, seleccionar un volumen específico (0.5L, 1L, 2L) y pagar mediante dos métodos: **Monedero Físico (Multicoin)** o **Pasarela Web (Simulacion)**.

## 🛠️ 2. Entorno de Desarrollo (Stack Tecnológico)
* **IDE:** Visual Studio Code (VS Code).
* **Framework y Gestor:** PlatformIO usando el framework de Arduino (C++).
* **Gestión de Archivos Web:** LittleFS y ESPAsyncWebServer para la interfaz de pago online.
* **Manejo de Librerías:** Durante el desarrollo de cada módulo, se evaluará y seleccionará la librería más eficiente y ligera para cada hardware (ej. `LiquidCrystal_I2C` para la pantalla, librerías de interrupciones para el flujo, etc.).

## ⚙️ 3. Componentes de Hardware y su Función General
1. **Microcontrolador Master (ESP32 - 38 pines):** Cerebro lógico del sistema. Maneja la interfaz de usuario, la máquina de estados, el servidor web y procesa los datos.
2. **Actuador de Potencia (PLC Industrial):** Actúa como esclavo de potencia. Sus relés de 10A encienden y apagan las bombas de 12V/24V.
3. **Placa de Optoacopladores:** Aísla eléctricamente las señales de 3.3V del ESP32 hacia las entradas lógicas (12V/24V) del PLC, protegiendo el microcontrolador.
4. **Sensores de Flujo (3x YF-S201):** Ubicados en cada línea de líquido. Envían pulsos digitales al ESP32 para calcular con exactitud los mililitros dispensados.
5. **Pantalla LCD 20x4 (I2C):** Muestra la interfaz de usuario, menús, saldos y mensajes de error temporales.
6. **Sistema de Pago (Monedero Multicoin):** Configurado para enviar pulsos al ESP32. Cuenta con un pin inhibidor (`SET`) para bloquear la entrada de dinero si la máquina no está en modo de cobro.
7. **Botonera de Navegación (4 Botones):** Ubicados en el panel principal. Sirven para navegar por la pantalla, seleccionar volumen, confirmar y cancelar.
8. **Botonera de Dispensado (3 Botones):** Ubicados en cada boquilla de salida. Solo se habilitan cuando el pago ha sido exitoso y determinan el inicio del bombeo físico.

## 🎯 4. Objetivos y Metodología de Trabajo
* **Desarrollo Modular:** El código se construirá y probará en módulos separados:

1. LCD y Menú 
2. Monedero
3. Sensor de flujo
4. Servidor Web(simulacion de pago online)

Esto se trabjara  por separado primero luego se uniara todo en uno.

* **Seguridad (Anti-Errores):** Prevenir dispensados accidentales, bloqueos por falta de líquido y desajustes de concurrencia (ej. un usuario presionando botones mientras otro paga por web).
* **Optimización:** Reutilizar funciones y código de renderizado (pantallas genéricas) para ahorrar memoria RAM y Flash en el ESP32.
