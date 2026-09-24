# 🧼 Dispensador Automático de Detergentes DE BRAYAN

## 📌 1. Descripción del Proyecto

Este proyecto consiste en el desarrollo del firmware para una máquina expendedora automática de líquidos diseñada para dispensar 3 tipos de detergentes a granel. 

El sistema permite a los usuarios:
* **Seleccionar el tipo de detergente.**
* **Elegir un volumen específico** entre tres opciones disponibles por producto (0.5L, 1L y 2L).
* **Pagar mediante dos métodos:** Monedero físico (Multicoin) o una pasarela de pago Web simulada mediante código QR.

## 🛠️ 2. Entorno de Desarrollo (Stack Tecnológico)

* **IDE:** Visual Studio Code (VS Code).
* **Framework y Gestor:** PlatformIO utilizando el framework de Arduino (C++).
* **Gestión de Archivos Web:** LittleFS y `ESPAsyncWebServer` para servir la interfaz de pago online, la cual estará disponible únicamente cuando se seleccione el método de pago por QR.
* **Manejo de Librerías:** Durante el desarrollo de cada módulo, se selecciona la librería más eficiente y ligera para cada componente de hardware (pantalla, interrupciones de flujo, etc.).

## ⚙️ 3. Componentes de Hardware y su Función General

1. **Microcontrolador Master (ESP32 - 38 pines):** Es el cerebro lógico del sistema. Maneja la máquina de estados, el servidor web, procesa los datos y toma las decisiones de dispensado.
2. **Interfaz HMI (Arduino Uno + Pantalla TFT LCD 2.4''):** Muestra la interfaz gráfica de usuario, los menús y el saldo. Se comunica de forma bidireccional por UART con el ESP32.
3. **Actuador de Potencia (PLC Industrial):** Actúa como esclavo de potencia. Sus relés de 10A se encargan de encender y apagar las bombas de 12V/24V.
4. **Placa de Optoacopladores:** Aísla eléctricamente las señales de control de 3.3V (ESP32) hacia las entradas lógicas de 12V/24V del PLC, protegiendo al microcontrolador.
5. **Medición de Líquidos:**
   * **Sensores de Flujo (2x YF-S201):** Ubicados en dos de las líneas de líquido. Envían pulsos digitales al ESP32 (leídos por interrupción) para calcular con exactitud los mililitros dispensados.
   * **Temporizador (1x):** La tercera línea de producto (alta viscosidad) se controla por tiempo de bombeo, utilizando 3 tiempos independientes y configurables según el volumen seleccionado (0.5L, 1L y 2L).
6. **Sistema de Pago (Monedero Multicoin):** Configurado para enviar pulsos al ESP32. Cuenta con un pin `SET` para deshabilitar y bloquear físicamente la entrada de monedas si la máquina no está en la pantalla de cobro por efectivo.
7. **Botonera de Navegación:** Botones táctiles integrados directamente en la pantalla HMI.
8. **Botonera de Dispensado (3 Pulsadores Físicos):** Ubicados en cada boquilla de salida. Solo se habilitan cuando el pago ha sido exitoso e inician el bombeo del líquido comprado.
9. **Sensores de Nivel (3 Interruptores Flotadores):** Operan con lógica inversa (cierran circuito cuando no hay líquido y abren cuando hay líquido). Esta señal indica al sistema qué opciones de productos ocultar en la pantalla HMI.
10. **Botón de Despertar (1 Pulsador):** Permite reactivar la máquina desde el modo de bajo consumo. Tras "X" minutos de inactividad, el sistema apaga la pantalla HMI para ahorrar energía (sin perder los datos de saldo ni la memoria RAM) y muestra un mensaje para presionar este botón.

## 🎯 4. Objetivos y Metodología de Trabajo

* **Desarrollo Modular:** El código se construye y prueba en módulos separados antes de la integración final:
  1. HMI (Arduino Uno) - ESP32: Comunicación UART sincronizada.
  2. Monedero (Lógica e interrupciones) - *Completado.*
  3. Sensores de flujo - *Completado.*
  4. Servidor Web (Simulación de pago online) - *Pendiente de restricciones de estado.*
* **Optimización:** Reutilizar funciones y simplificar el código de renderizado visual para ahorrar memoria RAM y Flash, garantizando la estabilidad tanto en el ESP32 como en el Arduino Uno.
