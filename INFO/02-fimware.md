# Dispensador de Detergentes — Documentación técnica completa

---

## 1. Qué es el sistema

Máquina expendedora de 3 detergentes líquidos, en volúmenes de 0.5L, 1L o
2L, con dos formas de pago: **QR (web simulada)** o **moneda física**. Corre
en dos microcontroladores separados que se hablan por UART:

| Placa                             | Rol                                                           | Archivo               |
| --------------------------------- | ------------------------------------------------------------- | --------------------- |
| **ESP32** (38 pines)              | Controlador principal y única autoridad de negocio            | `src/main.cpp`        |
| **Arduino Uno + TFT 2.4" táctil** | Pantalla (HMI). No decide nada, solo muestra y reporta toques | `hmi_arduino_uno.ino` |

**El ESP32 manda, el Arduino obedece**. El Arduino nunca valida
saldo, precios ni nada de negocio — solo dibuja lo que el ESP32 le dice y le
avisa qué botón tocaron.

Hay también una interfaz web (`data/index.html`), servida por el propio
ESP32, que es la pantalla de pago cuando el cliente elige el método QR
(se conecta al WiFi del ESP32 con el celular).

---

## 2. Mapa de pines (ESP32)

| Pin | Función                              | Modo                                                            |
| --- | ------------------------------------ | --------------------------------------------------------------- |
| 18  | Monedero, lectura de pulsos (`COIN`) | `INPUT`, interrupción `FALLING`                                 |
| 19  | Monedero, bloqueo (`SET`)            | `OUTPUT`. `HIGH` = habilitado, `LOW` = bloqueado                |
| 21  | UART2 RX (recibe del Arduino/HMI)    | `Serial2`, 9600 baudios                                         |
| 22  | UART2 TX (envía al Arduino/HMI)      | `Serial2`, 9600 baudios                                         |
| 17  | Bomba 1 (hacia PLC)                  | `OUTPUT`. `HIGH` = encendida                                    |
| 16  | Bomba 2 (hacia PLC)                  | `OUTPUT`. `HIGH` = encendida                                    |
| 4   | Bomba 3 (hacia PLC)                  | `OUTPUT`. `HIGH` = encendida                                    |
| 32  | Botón físico boquilla 1              | `INPUT_PULLUP`                                                  |
| 33  | Botón físico boquilla 2              | `INPUT_PULLUP`                                                  |
| 25  | Botón físico boquilla 3              | `INPUT_PULLUP`                                                  |
| 26  | Flotador nivel tanque 1              | `INPUT_PULLUP`. `LOW` = vacío, `HIGH` = con líquido             |
| 27  | Flotador nivel tanque 2              | `INPUT_PULLUP`                                                  |
| 14  | Flotador nivel tanque 3              | `INPUT_PULLUP`                                                  |
| 13  | Botón físico de despertar            | `INPUT_PULLUP`, wake source de deep sleep (`ext0`, nivel `LOW`) |
| 35  | Sensor de flujo bomba 1              | `INPUT`, interrupción `FALLING` (pull-up externo obligatorio)   |
| 34  | Sensor de flujo bomba 2              | `INPUT`, interrupción `FALLING` (pull-up externo obligatorio)   |

El **producto 3 no tiene sensor de flujo** (líquido de alta viscosidad): se
dispensa por tiempo fijo.

El ESP32 escribe `HIGH` para activar cada bomba. La inversión
a `LOW` que necesita físicamente el PLC ocurre en la etapa de
optoacoplador.

---

## 3. WiFi / servidor web

- SSID: `ESP32-DEV`, contraseña: `1234567890`, IP fija: `192.168.4.1`.
- El AP + servidor **no están siempre prendidos**. Se encienden solo cuando
  el cliente elige método de pago QR (`iniciarServidorWeb()`), y se apagan
  al volver a reposo (`detenerServidorWeb()`).
- Portal cautivo: hay rutas de detección de cada sistema operativo
  (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`, etc.) que
  redirigen todas a `/`.
- El frontend (`data/index.html`) se sirve desde LittleFS.

### Endpoints HTTP

| Ruta         | Método | Qué hace                                                                                                                                |
| ------------ | ------ | --------------------------------------------------------------------------------------------------------------------------------------- |
| `/productos` | GET    | Lista los 3 productos con precio por litro y disponibilidad (según sensores de nivel)                                                   |
| `/estado`    | GET    | Estado actual para que la web haga polling: `libre`, `pagando`, `pago_exitoso`, `dispensando`, `finalizado`                             |
| `/pagar`     | POST   | Body `{producto, litros}`. `403` si el método activo no es QR, `503` si el sistema está ocupado, `200` si arranca la simulación de pago |

No existe (y no debe existir) ningún endpoint para disparar el dispensado
desde la web eso solo lo hace el botón físico de la boquilla correcta.

---

## 4. Máquina de estados (autoridad: ESP32)

```
REPOSO
  ├─ (QR)     -> QR_ESPERA -> QR_PAGANDO (5s) -> PAGO_EXITOSO
  └─ (MONEDA) -> MONEDA_INGRESO -> SEL_PROD -> SEL_VOL -> CONFIRMACION -> PAGO_EXITOSO

PAGO_EXITOSO -> DISPENSANDO -> FINALIZADO -> REPOSO (o MONEDA_INGRESO si sobró saldo)
```

- `PAGO_EXITOSO` es un estado **compartido** por ambos métodos de pago.
- El dispensado real (`DISPENSANDO`) solo lo dispara el botón físico de la
  boquilla que corresponde al producto pagado (`chequearBotonesBoquilla()`).
- Corte del dispensado:
  - Producto 1 y 2: por **pulsos del sensor de flujo** (`FACTOR_PULSOS_POR_LITRO = 530.0`, calibrar según el sensor real).
  - Producto 3: por **tiempo fijo** (`TIEMPO_DISP_MS = 10000` ms).
  - Corte de seguridad para todos: `TIEMPO_MAX_DISPENSADO_MS = 30000` ms (por si el sensor de flujo falla y nunca llega al objetivo).

### Tiempos configurables

| Constante                   | Valor | Qué es                                                                          |
| --------------------------- | ----- | ------------------------------------------------------------------------------- |
| `TIEMPO_PAGO_MS`            | 5000  | Simulación de "procesando pago" en QR                                           |
| `TIEMPO_DISP_MS`            | 10000 | Dispensado del producto 3 (sin sensor)                                          |
| `TIEMPO_MAX_DISPENSADO_MS`  | 30000 | Corte de seguridad si el sensor de flujo no responde                            |
| `TIEMPO_FIN_MS`             | 5000  | Cuánto se muestra "retire su producto"                                          |
| `TIEMPO_INACTIVIDAD_MS`     | 3 min | Sin actividad -> deep sleep                                                     |
| `TIEMPO_CONFIRMAR_VACIO_MS` | 3000  | Ventana para confirmar que un tanque está vacío (antirrebote de los flotadores) |
| `TIEMPO_CONTEO_MONEDA`      | 1000  | Ventana para contar los pulsos de una moneda insertada                          |

---

## 5. Monedero

- Denominaciones: 1, 2 y 5 Bs (identificadas por cantidad de pulsos: 2, 4 y 7 respectivamente). **No da vuelto.**
- El pin `SET` solo se habilita (`HIGH`) cuando el estado es
  `MONEDA_INGRESO`, `SEL_PROD` o `SEL_VOL` (`actualizarMonederoHabilitado()`).
- El saldo (`saldo`, entero, en Bs) vive **solo en RAM** — no se guarda en
  memoria no volátil (NVS). Se pierde si el ESP32 se reinicia o entra en
  deep sleep con saldo, aunque esto último está bloqueado a propósito (ver
  sección de sleep).

---

## 6. Precios y productos

- 3 productos, precios en **Bs, enteros y pares** (para que el precio de
  medio litro nunca dé decimales): hoy son 4, 6 y 8 Bs/L.
- Cantidades permitidas: 0.5L, 1L, 2L.
- Cada producto tiene un flotador de nivel asociado; si está vacío, se
  oculta/inhabilita tanto en la web (`/productos` con `disponible:false`)
  como en el HMI (máscara que se manda en `EST:SEL_PROD`).

---

## 7. Deep sleep

- Es sleep **hardware** (`esp_deep_sleep_start()`): corta WiFi, servidor, CPU, todo.
- Solo se activa si `puedeDormir()` es verdadero: estado `REPOSO` o
  `QR_ESPERA`, **y** `saldo == 0`. Si hay plata metida sin gastar o una
  compra a mitad de camino, no duerme (perdería esos datos al reiniciar).
- Único wake source: botón físico en GPIO13, por `ext0` en nivel `LOW`
  (con `rtc_gpio_pullup_en` configurado antes de dormir).
- Al despertar, el ESP32 **reinicia por completo** (vuelve a `setup()`),
  o sea que siempre despierta en `REPOSO`, no retoma el estado anterior.

---

## 8. Sensores de nivel (flotadores)

- 3 flotadores, lógica inversa: `LOW` = tanque vacío, `HIGH` = con líquido.
- Antirrebote por software: hace falta que se mantenga en `LOW` durante
  `TIEMPO_CONFIRMAR_VACIO_MS` (3s) seguidos para confirmarlo como vacío.
  Volver a `HIGH` se toma como inmediato (sin ventana de confirmación).
- Si un producto está dispensando y su tanque se vacía a mitad de camino,
  **no se corta el dispensado** — sigue hasta terminar. El producto recién
  se oculta para la siguiente compra.

---

## 9. Protocolo de comunicación ESP32 <-> Arduino (HMI)

UART2 del ESP32 (pines 21/22) hacia el `Serial` por defecto del Arduino
Uno (pines 0/1), 9600 baudios, sin reintento: son comandos de texto
plano de una línea, terminados en `\n`.

### ESP32 -> Arduino (qué mostrar)

| Comando                                         | Significado                                                               |
| ----------------------------------------------- | ------------------------------------------------------------------------- |
| `EST:REPOSO`                                    | Pantalla de inicio, elegir método de pago                                 |
| `EST:QR_ESPERA`                                 | Mostrar QR, esperando que paguen desde el celular                         |
| `EST:QR_PAGANDO`                                | Simulación de pago en curso (5s)                                          |
| `EST:PAGO_EXITOSO:<prod>:<vol>:<costo>`         | Pago confirmado, esperando que presionen la boquilla física               |
| `EST:MONEDA_INGRESO:<saldo>`                    | Esperando monedas                                                         |
| `EST:SEL_PROD:<saldo>:<mascara>`                | Elegir producto. `mascara` son 3 dígitos (1=disponible, 0=vacío)          |
| `EST:SEL_VOL:<prod>:<saldo>`                    | Elegir cantidad                                                           |
| `EST:CONFIRMACION:<prod>:<vol>:<costo>:<saldo>` | Confirmar compra                                                          |
| `EST:DISPENSANDO`                               | Dispensando (sin tiempo: corta el sensor de flujo, no un timer)           |
| `EST:FINALIZADO:<costo>`                        | Listo, retirar producto                                                   |
| `EST:SLEEP`                                     | Se manda justo antes de entrar en deep sleep                              |
| `SALDO:<valor>`                                 | Refresco rápido de saldo sin cambiar de pantalla (al insertar una moneda) |

El ESP32 además reenvía su estado actual **cada 2 segundos** (heartbeat),
haya cambiado algo o no, para que si el Arduino se reinicia solo, se
resincronice sin intervención.

### Arduino -> ESP32 (qué pasó)

| Comando       | Significado                                                                                                     |
| ------------- | --------------------------------------------------------------------------------------------------------------- |
| `TOQUE:<1-4>` | Se tocó el botón `n` (el ESP32 decide qué significa según su estado actual)                                     |
| `SYNC`        | El Arduino lo manda apenas arranca, para pedir el estado real ya mismo (en vez de esperar el próximo heartbeat) |

### Convención de los botones (`<1-4>`)

No es una grilla fija de cuadrantes — cada pantalla del Arduino define sus
propias zonas táctiles, pero el número que manda debe coincidir exacto con
lo que el ESP32 espera para ese estado. Ejemplos:

- `REPOSO`: 1 = QR, 2 = Moneda
- `QR_ESPERA`: solo 4 = Cancelar
- `SEL_PROD` / `SEL_VOL`: 1, 2, 3 = opciones; 4 = Atrás
- `CONFIRMACION`: 1 = Aceptar, 4 = Cancelar

Si se agrega o cambia una pantalla, ambos lados (`main.cpp` y
`hmi_arduino_uno.ino`) tienen que mantenerse sincronizados a mano — no hay
generación automática del protocolo.

---

## 10. Frontend web (`data/index.html`)

Un solo archivo HTML con CSS y JS embebidos (sin dependencias externas,
para que entre cómodo en LittleFS). Flujo: elegir producto → elegir
cantidad → pagar → esperar (polling a `/estado`) → pago exitoso → esperar
a que se dispense (botón físico) → listo. Los montos se muestran en Bs,
sin decimales (los precios son enteros).

---

## 11. Cosas pendientes / a definir

- **Saldo en RAM, no en NVS.** Si esto importa para producción, hay que
  agregar persistencia (`Preferences.h`).
- **Calibración real de `FACTOR_PULSOS_POR_LITRO`** (hoy 530.0, valor de
  prueba) — falta medir con los sensores YF-S201 reales instalados.
- Falta decidir si el deep sleep debería poder ocurrir en más estados
  además de `REPOSO`/`QR_ESPERA` (hoy está restringido por seguridad,
  para no perder plata en juego al reiniciar).
