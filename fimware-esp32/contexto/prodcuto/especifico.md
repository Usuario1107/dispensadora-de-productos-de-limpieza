#  Documentación de Especificaciones Técnicas y de Interfaz: Dispensador de Detergentes

Este documento detalla paso a paso la arquitectura de control, el comportamiento de la interfaz de usuario en el LCD 20x4, la gestión de botones físicos de acción directa y el control del hardware periférico (Monedero, PLC y Servidor Web).

---

Precio por litro de los prodcutos son enteros por litro y par 

##   1. Arquitectura de Control y Máquina de Estados (FSM)

El sistema opera bajo una **Máquina de Estados Finitos (FSM)** estricta. Esto significa que el ESP32 se encuentra siempre en un **único estado a la vez**, dictando de forma centralizada qué componentes están activos, qué botones se escuchan y qué pasarelas de pago están permitidas. 

### Resumen de Estados del Sistema

| Estado | Nombre del Estado | Pantalla LCD 20x4 | Botones Activos del Panel | Estado del Monedero (`PIN_SET`) | Estado del Servidor Web (QR) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1** | **Reposo (Inicio)** | Precios y opción de elegir método de pago | `[1]` Pago QR<br>`[2]` Pago Moneda | **INHIBIDO** (Rechaza monedas) | **BLOQUEADO** (Deniega peticiones POST tiene un indicador sobre el metodo de pago que esta actualmente y no puede aceptar pagos) |
| **2** | **QR - Espera** | "Escanee el QR desde su celular" | `[4]` Salir / Cancelar | **INHIBIDO** | **HABILITADO** (Acepta pago online) |
| **3** | **QR - Pagado** | "Pago Exitoso. Presione boton de su producto uno de los tres se habilita" | **NINGUNO** (Mostrar info del pago) | **INHIBIDO** | **BLOQUEADO** |
| **4** | **Moneda - Ingreso** | Muestra saldo actual en tiempo real | `[4]` Salir *(si saldo = 0)*<br>`[4]` Comprar *(si saldo > 0)* | **HABILITADO** (0V / Acepta pulsos) | **BLOQUEADO** |
| **5** | **Comprar - Selección Prod.** | Muestra saldo y los 3 productos | `[1]` Prod 1<br>`[2]` Prod 2<br>`[3]` Prod 3<br>`[4]` Atrás | **HABILITADO** | **BLOQUEADO** |
| **6** | **Moneda - Selección Vol.** | Muestra volúmenes (0.5L, 1L, 2L) | `[1]` 0.5L<br>`[2]` 1.0L<br>`[3]` 2.0L<br>`[4]` Atrás | **HABILITADO** | **BLOQUEADO** |
| **7** | **Moneda - Confirmación** | Resumen de compra y costo total | `[1]` Confirmar<br>`[4]` Atrás | **INHIBIDO** | **BLOQUEADO** |
| **8** | **Dispensado** | "Dispensando... Por favor espere" | **NINGUNO** (Se usan los pulsadores de las boquillas) | **INHIBIDO** | **BLOQUEADO** |

---

## 🎛️ 2. Diseño y Comportamiento de la Interfaz de Usuario (UI)

Para evitar desajustes visuales y errores operativos, la interfaz **no utiliza cursores flotantes ni menús de desplazamiento complejo**. Se basa estrictamente en **Botones de Acción Directa** ubicados en el panel principal junto al LCD:

* **`[1]` Botón 1:** Acción primaria (Ej. Seleccionar QR, Producto 1 o 0.5L).
* **`[2]` Botón 2:** Acción secundaria (Ej. Seleccionar Moneda, Producto 2 o 1.0L).
* **`[3]` Botón 3:** Acción terciaria (Ej. Seleccionar Producto 3 o 2.0L).
* **`[4]` Botón 4:** Botón multifunción dinámico (Cambia de etiqueta y función según el estado: actúa como *Salir*, *Comprar*, *Confirmar* o *Atrás*).

### Lógica de los Botones Físicos (Robustez contra Errores)
1. **Detección por Flanco de Bajada (*Falling Edge*):** El sistema lee únicamente el milisegundo exacto en que el pin pasa de `HIGH` a `LOW` gracias a las resistencias internas `INPUT_PULLUP`. Mantener el botón presionado no repetirá la acción.
2. **Antirrebote y Bloqueo Global por Software:** Al detectarse una pulsación válida, se activa una bandera de bloqueo temporal de ~300 ms mediante `millis()`, ignorando rebotes mecánicos y pulsaciones simultáneas accidentales.
3. **Máscaras de Estado:** En cada estado de la FSM, el código descarta físicamente cualquier lectura proveniente de botones que no estén explícitamente autorizados para esa pantalla.

---

## 🔒 3. Control de Hardware y Seguridad de Pagos

### A. Gestión del Monedero Multicoin (`PIN_SET`)
* **Inhibido (`LOW` / 0V):** Envía una señal de bloqueo que obliga al monedero a rechazar cualquier moneda insertada, desviándola a la bandeja de devolución. Se activa en el Estado de Reposo, durante el flujo QR y en momentos críticos de cálculo matemático para evitar saltos de saldo.
* **Habilitado (`HIGH` / 3.3V):** Permite el paso de monedas. Sus pulsos físicos se leen mediante una interrupción (`ISR`) en el ESP32 para actualizar el saldo en tiempo real en la Pantalla 4.

### B. Gestión del Servidor Web (Pagos por QR)
* El servidor asíncrono (`ESPAsyncWebServer`) se mantiene ejecutándose en segundo plano en la red Wi-Fi del ESP32, pero sus endpoints de cobro (`/api/pagar`) operan bajo un **candado de estado**.
* Si el sistema no se encuentra activamente en el **Estado 2 (QR - Espera)**, cualquier intento de pago desde un navegador web móvil será rechazado con un código HTTP de prohibición. Una vez que el usuario presiona el botón físico de QR en la máquina, el ESP32 abre la pasarela temporalmente hasta que se procesa el cobro o se cancela la operación.

---

## ⚙️ 4. Fase de Dispensado y Comunicación con el PLC

Independientemente de si el usuario pagó por Moneda o por QR, el proceso de entrega de líquido sigue un ciclo idéntico:

1. **Liberación de Boquilla:** El panel central se deshabilita y el LCD indica al usuario que presione el pulsador físico ubicado exactamente en la boquilla del producto correspondiente.
2. **Activación de Potencia:** Al presionar el botón de la boquilla, el ESP32 envía una señal de salida (`HIGH`) a través de la **placa de optoacopladores**, la cual conmuta la entrada digital del **PLC**.
3. **Bombeo y Medición:** El PLC activa su relé de 10A para encender la bomba de 12V. Simultáneamente, el sensor de flujo **YF-S201** correspondiente envía una cadena de pulsos al ESP32 por interrupción.
4. **Corte y Seguridad (Timeout):** 
   * Al alcanzar el número exacto de pulsos equivalentes al volumen comprado, el ESP32 apaga la señal al PLC, deteniendo la bomba.
   
## TODO ES TEORICO ACTUALMENTE EN EL PROCESO PUEDE CAMBIAR DE ACUERDO A LAS SITUACIONES
