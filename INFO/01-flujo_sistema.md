# Flujo General del Sistema y Experiencia de Usuario

Este documento describe, en términos generales y sin entrar en código, cómo
funciona la máquina dispensadora de detergentes: la secuencia de pantallas
que ve el usuario, el comportamiento del ESP32 como controlador central, y
la gestión de los dos métodos de pago (moneda y QR).

---

## 1. Arquitectura general

El sistema está compuesto por dos placas que se comunican entre sí:

- **ESP32:** es el controlador central. Decide en qué estado está la
  máquina, valida el saldo, controla el monedero, las bombas, los
  sensores y el servidor web. Es la única autoridad del sistema.
- **Arduino Uno + pantalla táctil TFT:** es la interfaz visual (HMI). No
  toma ninguna decisión por sí mismo: únicamente muestra la pantalla que
  el ESP32 le indica y le informa qué botón tocó el usuario.

La comunicación entre ambas placas es por UART (dos hilos, TX y RX) a
9600 baudios. El ESP32 envía el estado a mostrar y el Arduino responde
con los toques del usuario. El ESP32 además reenvía su estado actual de
forma periódica, de manera que si el Arduino se reinicia por cualquier
motivo, la pantalla se resincroniza sola sin intervención manual.

---

## 2. Saldo de moneda

El saldo ingresado por el usuario se mantiene en la memoria de trabajo
del ESP32 mientras la máquina está encendida: no se borra al volver a la
pantalla de inicio, al cambiar de producto, ni si sobra dinero después
de una compra.

**Estado actual:** el saldo todavía no se guarda en memoria permanente
(NVS/flash). Esto significa que sobrevive a cualquier navegación normal
dentro del sistema, pero se pierde si la máquina se reinicia o pierde
alimentación eléctrica. Agregar persistencia en memoria no volátil es una
mejora pendiente.

Por este mismo motivo, la máquina **no entra en su modo de bajo consumo**
si hay saldo pendiente sin gastar: prioriza no perder el dinero del
usuario antes que ahorrar energía.

---

## 3. Reglas generales de interacción

- Cada toque en la pantalla se procesa una sola vez gracias a un control
  de tiempo mínimo entre toques, evitando que un mismo toque dispare
  varias acciones o saltos accidentales entre pantallas.
- La interfaz es mayormente estática: las únicas partes que cambian en
  tiempo real dentro de una misma pantalla son el saldo acumulado y la
  disponibilidad de cada producto (agotado o no).
- El monedero físico solo puede recibir monedas cuando la máquina está en
  una pantalla del flujo de pago por moneda; en cualquier otro momento
  está bloqueado, incluidas todas las pantallas del flujo QR.

---

## 4. Flujo paso a paso — Pago por moneda

**Inicio → Selección de producto → Selección de volumen → Confirmación → Instrucción de boquilla → Dispensado**

### 4.1 Pantalla de inicio

El usuario elige entre pagar con moneda o con QR. Si hay saldo de una
sesión anterior sin usar, se muestra en esta pantalla.

### 4.2 Selección de producto

Se muestran los 3 detergentes disponibles. Si el nivel de algún tanque
está bajo, ese producto se marca como agotado y no puede seleccionarse.
El monedero está habilitado en esta pantalla: cualquier moneda insertada
actualiza el saldo en el momento. También hay un botón para volver al
inicio sin perder el saldo acumulado.

### 4.3 Selección de volumen

Se eligen los litros a comprar (0.5L, 1L o 2L). Al elegir una opción, el
sistema calcula el costo de inmediato:

- **Si el saldo alcanza:** avanza a la pantalla de confirmación.
- **Si el saldo no alcanza:** se muestra un aviso en el momento y el
  usuario permanece en esta misma pantalla, pudiendo insertar más
  monedas o volver atrás a elegir un producto más económico.

### 4.4 Confirmación de compra

Se muestra el costo total y el saldo actual, con un botón para aceptar y
otro para volver a elegir el volumen. Al confirmar, el monedero se
bloquea de inmediato para evitar que sigan entrando monedas durante el
resto del proceso, y el saldo se descuenta.

### 4.5 Instrucción de boquilla

El sistema indica qué botón físico de boquilla presionar, según el
producto pagado. La pantalla permanece así hasta que se presiona el
botón correcto; presionar el de otro producto no tiene efecto.

### 4.6 Dispensado

Se activa la bomba correspondiente. La pantalla queda fija en "Dispensando,
por favor espere" y no responde a toques durante este tiempo. El corte del
llenado se hace por sensor de flujo en los productos que lo tienen, o por
un tiempo fijo en el producto de alta viscosidad (que no cuenta con
sensor).

### 4.7 Finalización

Se apaga la bomba, se muestra un mensaje breve de compra exitosa con el
saldo restante, y tras unos segundos la máquina vuelve sola: a la
pantalla de selección de producto si quedó saldo sin usar, o a la
pantalla de inicio si no quedó nada.

---

## 5. Flujo paso a paso — Pago por QR

**Inicio → HMI muestra QR / activa WiFi → Pago simulado en la web → Instrucción de boquilla → Dispensado**

### 5.1 Activación del modo QR

Al elegir esta opción, el ESP32 recién en ese momento enciende su red
WiFi propia y el servidor web — no están activos de forma permanente. La
pantalla muestra el código QR de acceso a la red. El monedero permanece
bloqueado durante todo este flujo.

### 5.2 Conexión y pago desde el celular

El usuario escanea el QR, se conecta a la red del dispensador y accede a
la página de pago simulado, donde elige producto y cantidad. Varios
celulares pueden conectarse y navegar la página al mismo tiempo sin
restricción.

### 5.3 Concurrencia y bloqueo

En el instante en que un usuario confirma el pago desde su celular, esa
compra queda como la única activa. Si otro usuario intenta pagar
mientras tanto, su navegador recibe un aviso de que el sistema está
ocupado y debe esperar. Mientras esto ocurre, la pantalla del HMI ya
muestra el producto comprado y la instrucción de presionar la boquilla
correspondiente.

### 5.4 Dispensado

Igual que en el flujo de moneda: se activa la bomba del producto
correspondiente al presionar el botón físico de esa boquilla, y la
pantalla queda bloqueada hasta terminar.

### 5.5 Cierre del modo QR

Al finalizar el dispensado, el sistema apaga la red WiFi y el servidor
web, y vuelve a la pantalla de inicio. Cada compra por QR corresponde a
una sola conexión de red: para una nueva compra por este método, hay que
volver a seleccionar la opción QR desde el inicio.

---

## 6. Sensores de nivel de tanque

Cada tanque tiene un sensor de nivel que indica si queda o no líquido
disponible. Para evitar falsas lecturas por movimiento o salpicaduras
del líquido, el sistema exige que la lectura de "vacío" se mantenga
sostenida por unos segundos antes de darla por confirmada.

Si un tanque se vacía mientras su producto ya está siendo dispensado (en
medio de una compra ya pagada), el dispensado **no se interrumpe**: se
completa igual, ya que existe un margen de líquido adicional para estos
casos. El producto recién se oculta de la selección a partir de la
siguiente compra.

---

## 7. Modo de bajo consumo y botón de despertar

Tras un período sin ninguna interacción, la máquina entra en un modo de
bajo consumo real: apaga su red WiFi, el servidor y prácticamente todo
su funcionamiento, y solo puede reactivarse mediante el botón físico
dedicado a esto.

Como se mencionó en la sección de saldo, este modo **no se activa** si
hay dinero ingresado sin gastar, precisamente para no arriesgar esa
información. Al despertar, la máquina vuelve a la pantalla de inicio.

---

## 8. Resumen de estados

| Pantalla | Entrada permitida | Monedero | WiFi / Web | Interacción táctil |
|---|---|---|---|---|
| Inicio | Elegir QR o moneda | Bloqueado | Apagado | Habilitada |
| Selección de producto | Elegir producto / volver | Habilitado | Apagado | Habilitada |
| Selección de volumen | Elegir volumen / atrás | Habilitado | Apagado | Habilitada |
| Confirmación de compra | Aceptar / atrás | Bloqueado | Apagado | Habilitada |
| Esperando pago QR | Cancelar | Bloqueado | Encendido | Habilitada (solo cancelar) |
| Pago exitoso (QR o moneda) | Ninguna | Bloqueado | Según método | Deshabilitada — solo botón físico de boquilla |
| Dispensando | Ninguna | Bloqueado | Según método | Deshabilitada por completo |

---

## 9.  mejoras futuras

- Persistencia del saldo en memoria no volátil (hoy se pierde ante un
  reinicio o corte de energía).
- Calibración final de los sensores de flujo con los caudalímetros
  realmente instalados.
