# Módulo: Servidor Web / Pago Simulado por QR

**Estado: ✅ Probado y funcionando** (SoftAP + portal cautivo + flujo completo de pago simulado + dispensado simulado).

Este documento sirve de plantilla para los módulos siguientes (LCD, Monedero, Sensor de flujo): indica qué variables/funciones de este módulo son **globales** (deben ser compartidas o respetadas por los demás módulos cuando se integre todo) y cuáles son **solo de prueba interna**, creadas para poder simular este módulo de forma aislada y que **no deben pasar tal cual al proyecto final**.

---

## 1. Variables y funciones GLOBALES (para integrar con otros módulos)

Esto es lo que los demás módulos necesitan leer o modificar cuando se una todo el código.

### 1.1 Máquina de estados del sistema (`EstadoSistema`)

```cpp
enum EstadoSistema {
  LIBRE,
  PAGANDO,
  PAGO_EXITOSO,
  DISPENSANDO,
  FINALIZADO
};
EstadoSistema estadoActual;
```

- **Es el estado central de todo el sistema**, no solo del módulo web. El LCD debe leer `estadoActual` para saber qué pantalla mostrar, el monedero debe leerlo para saber si puede aceptar pulsos, etc.
- **Nadie más que el módulo "dueño" de cada transición debe escribir en `estadoActual` directamente** — para eso existe la función `iniciarDispensado()` (ver abajo) y las transiciones internas de `actualizarEstado()`. Si otro módulo necesita iniciar una transición (ej. el monedero confirmando que se completó el pago con moneda), debe hacerse a través de una función dedicada, no tocando la variable a mano, para evitar condiciones de carrera.

### 1.2 Método de pago activo (`MetodoPago`)

```cpp
enum MetodoPago {
  METODO_NINGUNO,
  METODO_QR,
  METODO_MONEDA
};
MetodoPago metodoActivo;
```

- **Esta variable la debe controlar el módulo de LCD/botonera** (Estado 1 - Reposo, según la doc general: el usuario elige `[1]` QR o `[2]` Moneda).
- El módulo web **solo lee** `metodoActivo`: si no es `METODO_QR`, cierra la pasarela (`403 Forbidden`).
- Cuando se integre el LCD, ese módulo pasa a ser quien **escribe** esta variable; el módulo web deja de tener el valor fijo en el código y pasa a reaccionar a lo que el LCD determine.

### 1.3 Datos de la transacción en curso

```cpp
float montoActual;
int productoActualId;
float litrosActuales;
unsigned long tiempoDispensadoActualSeg;
```

- Estas 4 variables representan **la compra actual**, sin importar si el pago fue QR o Moneda. El módulo de Monedero, al confirmar una compra, debe llenarlas de la misma forma que lo hace `handlePagarBody()` en este módulo.
- `tiempoDispensadoActualSeg` es el tiempo que se le va a pasar al módulo de PLC/sensor de flujo para cortar el bombeo (hoy es un valor fijo de prueba, luego el sensor de flujo lo reemplaza por conteo de pulsos reales).

### 1.4 Catálogo de productos

```cpp
struct Producto {
  int id;
  const char* nombre;
  float precioPorLitro;
  unsigned long tiempoDispensadoSeg;
};
Producto productos[3];
const float cantidadesPermitidas[3]; // 0.5L, 1L, 2L
```

- Fuente única de verdad de productos, precios y cantidades válidas. El LCD debe usar este mismo arreglo para mostrar precios en pantalla (no debe tener su propia copia separada, para evitar inconsistencias entre lo que se ve en el LCD y lo que se cobra en la web).

### 1.5 Función para disparar el dispensado

```cpp
bool iniciarDispensado(const char* origen);
```

- **Punto de entrada único y obligatorio** para pasar a `DISPENSANDO`. Cualquier módulo que dispare el dispensado (botón físico de boquilla, comando por LCD, etc.) debe llamar a esta función, no modificar `estadoActual` directamente.
- Ya contempla la validación de que solo se puede dispensar si el estado es `PAGO_EXITOSO` — evita dispensados accidentales o duplicados.
- El parámetro `origen` es solo para el log de debug (identifica si vino de la web, botón físico, etc.).

### 1.6 Función central de actualización de estado

```cpp
void actualizarEstado(); // llamar siempre dentro de loop()
```

- Maneja las transiciones automáticas por tiempo (`PAGANDO` → `PAGO_EXITOSO`, `DISPENSANDO` → `FINALIZADO`, `FINALIZADO` → `LIBRE`).
- Cuando se integren los demás módulos, esta función sigue siendo la única responsable de mover el estado por tiempo — no debe duplicarse esta lógica en otro módulo.

### 1.7 Configuración WiFi / Servidor / LittleFS

```cpp
const char* AP_SSID / AP_PASSWORD;
AsyncWebServer server(80);
DNSServer dnsServer;
```

- El servidor y el AP deben mantenerse tal cual — son necesarios sin importar qué otros módulos se agreguen, ya que la pasarela QR sigue viva en segundo plano.

---

## 2. Variables y lógica de PRUEBA (solo para simular este módulo por separado)

Esto se creó **únicamente para poder probar el módulo web de forma aislada**, sin depender de que existan LCD, monedero o botonera física todavía. Se debe reemplazar o eliminar cuando se integren los módulos reales.

### 2.1 `metodoActivo` con valor fijo en el código

```cpp
MetodoPago metodoActivo = METODO_QR; // <-- cambiar aca para probar
```

- **Temporal.** En el proyecto final, esta variable no debe tener un valor fijo — la va a escribir el módulo de LCD/botonera según lo que el usuario elija en el Estado 1 (Reposo).
- Por ahora, para probar el módulo web solo, se cambia manualmente entre `METODO_QR`, `METODO_MONEDA` o `METODO_NINGUNO` y se recompila.

### 2.2 Botón físico placeholder (`PIN_BOTON_DISPENSAR`)

```cpp
#define PIN_BOTON_DISPENSAR 0   // pin BOOT de la placa, usado solo de prueba
bool ultimoEstadoBoton = HIGH;
void chequearBotonFisico();
```

- **Temporal.** Usa el botón físico "BOOT" que ya trae la placa ESP32 Dev Module, para poder probar el disparo de dispensado sin tener conectada la botonera real de 3 botones (uno por boquilla).
- Cuando se conecte la botonera real (3 botones, uno por producto, según la doc específica), esta lógica se reemplaza por 3 pines reales, cada uno llamando a `iniciarDispensado()` con el producto correspondiente ya validado contra `productoActualId`.

### 2.3 Endpoint `/simular-boton`

```cpp
void handleSimularBoton(AsyncWebServerRequest *request);
server.on("/simular-boton", HTTP_POST, handleSimularBoton);
```

- **Temporal / debug.** Ruta HTTP que simula "ya presionaron el botón físico de la boquilla", para poder probar el flujo completo (pago → pago exitoso → dispensado → finalizado) desde el navegador sin ningún hardware conectado.
- En el HTML de prueba hay un botón "Simular dispensado (debug)" que llama a esta ruta.
- **Se debe quitar del código final** (o dejarlo oculto detrás de `#if DEBUG`) una vez que la botonera física esté integrada, para que nadie pueda disparar un dispensado real desde la web sin pagar.

### 2.4 Tiempos fijos de prueba

```cpp
tiempoDispensadoSeg = 10; // igual para los 3 productos
const unsigned long TIEMPO_PAGO_MS = 5000;
```

- **Temporal.** El tiempo de dispensado (10s parejo) es un placeholder hasta que el módulo de sensor de flujo (YF-S201) mida el volumen real por pulsos y corte el bombeo por cantidad, no por tiempo.
- El tiempo de "pago" (5s) sí es definitivo — es una simulación intencional de "procesando dinero", no depende de hardware.

### 2.5 Datos de productos con precios de ejemplo

```cpp
Producto productos[3] = {
  {1, "Detergente A", 5.00, 10},
  {2, "Detergente B", 6.50, 10},
  {3, "Detergente C", 8.00, 10}
};
```

- Los nombres y precios son de ejemplo. Quedan editables en una sola línea para cuando se definan los productos y precios reales del negocio.

---

## 3. Checklist de pruebas realizadas (este módulo)

- [x] SoftAP visible y conectable (`ESP32-DEV` / `1234567890`)
- [x] Portal cautivo redirige correctamente en distintas rutas de detección de SO
- [x] `LittleFS` sirve `index.html` sin errores
- [x] Selección de producto y cantidad (0.5L / 1L / 2L) calcula el monto correcto
- [x] `/pagar` bloquea con `403` cuando `metodoActivo != METODO_QR`
- [x] `/pagar` bloquea con `503` cuando el sistema no está `LIBRE` (otro cliente en proceso)
- [x] Simulación de pago dura 5 segundos fijos y pasa a `PAGO_EXITOSO`
- [x] Botón de debug "Simular dispensado" dispara `DISPENSANDO` solo si el estado es `PAGO_EXITOSO`
- [x] Botón físico BOOT (`GPIO0`) también dispara el dispensado (mismo camino que el debug web)
- [x] Tras `FINALIZADO`, el sistema vuelve solo a `LIBRE` sin intervención manual
- [x] Logs de Serial muestran conexión/desconexión de clientes WiFi (MAC) y cada evento de pago/rechazo con IP

---

## 4. Nota para los siguientes módulos

Cuando armes tu módulo (LCD, Monedero, Sensor de flujo), copiá esta misma estructura de documento:
1. Qué variables/funciones tuyas van a ser usadas por otros módulos (**Globales**).
2. Qué armaste solo para poder probar tu módulo aislado, que hay que sacar o reemplazar al integrar (**Prueba/Debug**).
3. Un checklist de qué probaste y confirmaste que funciona.
