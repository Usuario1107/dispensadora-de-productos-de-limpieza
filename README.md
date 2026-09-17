#  Documentación General: Dispensador Automático de Detergentes

## 📌 1. Descripción del Proyecto
Este proyecto consiste en el desarrollo del firmware para una máquina expendedora automática de líquidos (3 tipos de detergentes). El sistema permite comprar detergente en granel, Seleccionar el tipo de detergente y el volumen específico (0.5L, 1L, 2L) tres tipos de volumen para cada peoducto y pagar mediante dos métodos: **Monedero Físico (Multicoin)** o **Pasarela Web (Simulacion)**.

## 🛠️ 2. Entorno de Desarrollo (Stack Tecnológico)
* **IDE:** Visual Studio Code (VS Code).
* **Framework y Gestor:** PlatformIO usando el framework de Arduino (C++).
* **Gestión de Archivos Web:** LittleFS y ESPAsyncWebServer para la interfaz de pago online y estara disponible cuando seleccione el metodo de pago por QR.
* **Manejo de Librerías:** Durante el desarrollo de cada módulo, se evaluará y seleccionará la librería más eficiente y ligera para cada hardware (ej. para la pantalla, librerías de interrupciones para el flujo, etc.).

## ⚙️ 3. Componentes de Hardware y su Función General
1. **Microcontrolador Master (ESP32 - 38 pines):** Cerebro lógico del sistema, la máquina de estados, el servidor web y procesa los datos.
2. **Actuador de Potencia (PLC Industrial):** Actúa como esclavo de potencia. Sus relés de 10A encienden y apagan las bombas de 12V/24V.
3. **Placa de Optoacopladores:** Aísla eléctricamente las señales de 3.3V del ESP32 hacia las entradas lógicas (12V/24V) del PLC, protegiendo el microcontrolador.
4. **Sensores de Flujo (2x YF-S201):** Ubicados en cada dos línea de líquido. Envían pulsos digitales al ESP32 para calcular con exactitud los mililitros dispensados .
5. **Módulo shield pantalla TFT LCD 2.4'' Arduino** Muestra la interfaz de usuario, menús, saldos se comunica por UART eL ESP32 con el ARDUINO UNO(QUE CONTROLA EL HMI).
6. **Sistema de Pago (Monedero Multicoin):** Configurado para enviar pulsos al ESP32. Cuenta con un pin DESABILITAR (`SET`) para bloquear la entrada de dinero si la máquina no está en modo de cobro por moneda.
7. **Botonera de Navegación :** Se utlizan la botones tactiles en la pantlla HMI.
8. **Botonera de Dispensado (3 Botones):** Ubicados en cada boquilla de salida. Solo se habilitan cuando el pago ha sido exitoso y determinan el inicio del bombeo físico.
8. **Sensores de nivel interruptor (3 Sensores)** cierra cuando no hay liquido  ABRE cuando hay lirquido asi que la señal que se enviara pora hi hace eso para ocultar las opciones que hay disponible para seleccionar.
8. **Boton  de despertar (1 pulsador):** La funcion de este es caudno el usuario quiero comprar despierte todo el sitema de control  el sitema de control de dormira depues de x minutos puesto en el progmra depseus de que no hay uso de  manjero de la maquina esto para reducir el consumo .

## 🎯 4. Objetivos y Metodología de Trabajo
* **Desarrollo Modular:** El código se construirá y probará en módulos separados:

1. LCD y Menú ARDUINO UNO - ESP32 comunuacion sincronizado 
2. Monedero HECHO
3. Sensor de flujo HECHO
4. Servidor Web(simulacion de pago online) FALTA RESTRICCIONES

Esto se trabjara  por separado primero luego se uniara todo en uno.

* **Optimización:** Reutilizar funciones y código de renderizado (pantallas) para ahorrar memoria RAM y Flash en el ESP32 y ARDUINO.