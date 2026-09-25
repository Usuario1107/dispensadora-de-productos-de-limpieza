// DISPENSADOR DE DETERGENTES - ESP32 (controlador principal)
// El ESP32 es la UNICA autoridad de negocio: saldo, metodo de pago, validacion
// de compra, estados. El Arduino Uno + TFT solo muestra lo que este le manda
// y reporta toques. Ver protocolo UART al final del archivo.
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"

Preferences prefs; // guarda el saldo en memoria no volatil (NVS)

// DEBUG / SERIAL - siempre activo por ahora, se apaga para produccion final
#define DEBUG true

#if DEBUG
#define debugPrint(x) Serial.print(x)
#define debugPrintln(x) Serial.println(x)
#else
#define debugPrint(x)
#define debugPrintln(x)
#endif

void logSerial(String msg)
{
  debugPrintln(msg);
}

// MAPA DE PINES
const int PIN_COIN = 18;
const int PIN_COIN_SET = 19;

// UART2 hacia el HMI (Arduino Uno + TFT). RX2 recibe del HMI, TX2 le envia.
const int PIN_HMI_RX2 = 21;
const int PIN_HMI_TX2 = 22;
const long HMI_BAUDIOS = 9600;

const int PIN_PLC_BOMBA_1 = 17;
const int PIN_PLC_BOMBA_2 = 16;
const int PIN_PLC_BOMBA_3 = 4;

const int PIN_BTN_BOQ_1 = 32;
const int PIN_BTN_BOQ_2 = 33;
const int PIN_BTN_BOQ_3 = 25;

const int PIN_NIVEL_1 = 26; // flotador tanque 1 (LOW = vacio, HIGH = con liquido)
const int PIN_NIVEL_2 = 27;
const int PIN_NIVEL_3 = 14;

const int PIN_BOTON_DESPERTAR = 13;

// Reservados para uso futuro (ya no se usan como sensor de flujo). Quedan
// como entrada para no perder los pines, sin interrupcion ni logica.
const int INPUT_AUX_1 = 35;
const int INPUT_AUX_2 = 34;

// Tiempo base para 1 litro de cada producto, sin sensor de flujo.
const unsigned long TIEMPO_DISP_1L_PROD_1_MS = 8000UL; //6
const unsigned long TIEMPO_DISP_1L_PROD_2_MS = 8000UL; //5
const unsigned long TIEMPO_DISP_1L_PROD_3_MS = 8000UL; //4

unsigned long tiempoDispensadoDe(int prod, int vol)
{
  unsigned long tiempoBase;

  if (prod == 1)
    tiempoBase = TIEMPO_DISP_1L_PROD_1_MS;
  else if (prod == 2)
    tiempoBase = TIEMPO_DISP_1L_PROD_2_MS;
  else
    tiempoBase = TIEMPO_DISP_1L_PROD_3_MS;

  if (vol == 1)
    return tiempoBase / 2;
  if (vol == 2)
    return tiempoBase;
  return tiempoBase * 2;
}

// WIFI SOFTAP (editable) - se enciende solo bajo demanda (metodo QR)
const char *AP_SSID = "ESP32-DEV";
const char *AP_PASSWORD = "1234567890";
IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_GATEWAY(192, 168, 4, 1);
IPAddress AP_SUBNET(255, 255, 255, 0);

bool servidorActivo = false;

// PRODUCTOS - precios enteros y pares (Bs), confirmados
struct Producto
{
  int id;
  String nombre;
  int precioPorLitro;
};

Producto productos[3] = {
    {1, "Producto 6", 4}, // Producto 1
    {2, "Producto 5", 6}, // Producto 2
    {3, "Producto 4", 8}  // Producto 3
};

const float cantidadesPermitidas[3] = {0.5, 1.0, 2.0};

bool tanqueVacio[3] = {false, false, false}; // actualizado por los flotadores de nivel

Producto *buscarProducto(int id)
{
  for (int i = 0; i < 3; i++)
  {
    if (productos[i].id == id)
      return &productos[i];
  }
  return nullptr;
}

int calcularCosto(Producto *p, float litros)
{
  return (int)round(p->precioPorLitro * litros);
}

// MONEDERO (denominaciones 1, 2, 5 Bs - MEDIUN 50 MS o slow 100 ms)
#define TIEMPO_CONTEO_MONEDA 1000
#define PULSOS_1BS 2
#define PULSOS_2BS 4
#define PULSOS_5BS 7

volatile int pulsosContados = 0;
volatile bool hayPulsoMoneda = false;
bool conteoMonedaEnProceso = false;
unsigned long tInicioConteoMoneda = 0;

int saldo = 0;
bool monederoHabilitado = false; // lo controla la FSM segun el estado

void guardarSaldo()
{
  prefs.putInt("saldo", saldo);
}

// MAQUINA DE ESTADOS
enum EstadoSistema
{
  REPOSO,
  QR_ESPERA,
  QR_PAGANDO,   // simulacion de pago online (5s)
  PAGO_EXITOSO, // compartido por QR y moneda, esperando boton de boquilla
  QR_CONTINUAR, // post-dispensado QR: seguir con QR o cancelar (cierra wifi)
  MONEDA_INGRESO,
  SEL_PROD,
  SEL_VOL,
  CONFIRMACION,
  DISPENSANDO,
  FINALIZADO
};

EstadoSistema estadoActual = REPOSO;

enum MetodoPago
{
  METODO_NINGUNO,
  METODO_QR,
  METODO_MONEDA
};
MetodoPago metodoActivo = METODO_NINGUNO;

const char *nombreMetodo(MetodoPago m)
{
  switch (m)
  {
  case METODO_QR:
    return "qr";
  case METODO_MONEDA:
    return "moneda";
  default:
    return "ninguno";
  }
}

const char *nombreEstado(EstadoSistema e)
{
  switch (e)
  {
  case REPOSO:
    return "REPOSO";
  case QR_ESPERA:
    return "QR_ESPERA";
  case QR_PAGANDO:
    return "QR_PAGANDO";
  case PAGO_EXITOSO:
    return "PAGO_EXITOSO";
  case QR_CONTINUAR:
    return "QR_CONTINUAR";
  case MONEDA_INGRESO:
    return "MONEDA_INGRESO";
  case SEL_PROD:
    return "SEL_PROD";
  case SEL_VOL:
    return "SEL_VOL";
  case CONFIRMACION:
    return "CONFIRMACION";
  case DISPENSANDO:
    return "DISPENSANDO";
  case FINALIZADO:
    return "FINALIZADO";
  default:
    return "?";
  }
}

// Datos de la compra en curso (compartidos por QR y moneda)
int prodSel = 0;
int volSel = 0; // 1: 0.5L, 2: 1L, 3: 2L
int costoSel = 0;

const unsigned long TIEMPO_PAGO_MS = 5000;                       // simulacion de pago QR
const unsigned long TIEMPO_FIN_MS = 5000;                        // pantalla "retire su producto"
const unsigned long TIEMPO_INACTIVIDAD_MS = 2UL * 60UL * 1000UL; // min sin actividad -> deep sleep

unsigned long tInicioPagoQR = 0;
unsigned long tInicioDisp = 0;
unsigned long tInicioFin = 0;
unsigned long ultimaActividad = 0;

unsigned long tUltimoHeartbeat = 0;
const unsigned long HEARTBEAT_MS = 3000; // reenvia el estado actual siempre, por si el HMI se reinicio

// Botones de boquilla (polling con debounce)
bool antBoq1 = HIGH, antBoq2 = HIGH, antBoq3 = HIGH;
unsigned long tUltimoBoton = 0;
const unsigned long DEBOUNCE_MS = 250;

// Sensores de nivel (antirrebote por ventana de tiempo: 3s sostenido en LOW)
unsigned long tCandidatoVacio[3] = {0, 0, 0};
bool candidatoVacioActivo[3] = {false, false, false};
const unsigned long TIEMPO_CONFIRMAR_VACIO_MS = 3000;

// OBJETOS DEL SERVIDOR WEB Y HMI
DNSServer dnsServer;
AsyncWebServer server(80);
HardwareSerial &hmiSerial = Serial2;
String bufferHMI = "";

// PROTOTIPOS
void IRAM_ATTR ISR_Moneda();
int evaluarPulsosMoneda(int p);
void procesarMoneda();
void cambiarEstado(EstadoSistema nuevo);
void enviarEstadoHMI();
void enviarComandoHMI(String cmd);
void leerSerialHMI();
void manejarToqueHMI(int btn);
void chequearBotonesBoquilla();
bool puedeDormir();
void entrarDeepSleep();
void chequearSensoresNivel();
void iniciarServidorWeb();
void detenerServidorWeb();
bool iniciarDispensado(const char *origen);
void apagarBombas();
void activarBomba(int prod);
void actualizarEstadoTiempos();
void actualizarMonederoHabilitado();
String mascaraDisponibilidad();

// CAMBIO DE ESTADO (centralizado, siempre loguea y siempre avisa al HMI)
void cambiarEstado(EstadoSistema nuevo)
{
  logSerial("[ESTADO] " + String(nombreEstado(estadoActual)) + " -> " + String(nombreEstado(nuevo)));
  estadoActual = nuevo;
  ultimaActividad = millis(); // cualquier cambio de estado cuenta como actividad
  actualizarMonederoHabilitado();
  enviarEstadoHMI();
}

// SALIDAS AL PLC (BOMBAS) - activo en HIGH
void apagarBombas()
{
  digitalWrite(PIN_PLC_BOMBA_1, LOW);
  digitalWrite(PIN_PLC_BOMBA_2, LOW);
  digitalWrite(PIN_PLC_BOMBA_3, LOW);
}

void activarBomba(int prod)
{
  apagarBombas();
  if (prod == 1)
    digitalWrite(PIN_PLC_BOMBA_1, HIGH);
  else if (prod == 2)
    digitalWrite(PIN_PLC_BOMBA_2, HIGH);
  else if (prod == 3)
    digitalWrite(PIN_PLC_BOMBA_3, HIGH);
  logSerial("[PLC] Bomba " + String(prod) + " activada (HIGH)");
}

// MONEDERO
void IRAM_ATTR ISR_Moneda()
{
  pulsosContados++;
  hayPulsoMoneda = true;
}

int evaluarPulsosMoneda(int p)
{
  if (p == PULSOS_1BS)
    return 1;
  if (p == PULSOS_2BS)
    return 2;
  if (p == PULSOS_5BS)
    return 5;
  return 0;
}

void actualizarMonederoHabilitado()
{
  monederoHabilitado = (estadoActual == MONEDA_INGRESO || estadoActual == SEL_PROD || estadoActual == SEL_VOL);
  digitalWrite(PIN_COIN_SET, monederoHabilitado ? HIGH : LOW);
  logSerial(String("[MONEDERO] SET -> ") + (monederoHabilitado ? "HABILITADO (HIGH)" : "BLOQUEADO (LOW)"));
}

void procesarMoneda()
{
  detachInterrupt(digitalPinToInterrupt(PIN_COIN));
  int p = pulsosContados;
  pulsosContados = 0;
  conteoMonedaEnProceso = false;
  hayPulsoMoneda = false;

  if (!monederoHabilitado)
  {
    logSerial("[MONEDERO] Pulso ignorado, estado no valido para cobro");
    attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);
    return;
  }

  digitalWrite(PIN_COIN_SET, HIGH);
  attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);

  int valor = evaluarPulsosMoneda(p);
  if (valor > 0)
  {
    saldo += valor;
    guardarSaldo();
    ultimaActividad = millis();
    logSerial("[MONEDERO] Moneda aceptada: " + String(valor) + " Bs | Saldo: " + String(saldo) + " Bs");
    enviarComandoHMI("SALDO:" + String(saldo));
  }
  else
  {
    logSerial("[MONEDERO] Moneda no reconocida (pulsos: " + String(p) + ")");
  }
}

// SENSORES DE NIVEL (flotadores, antirrebote por ventana de 3s)
void chequearSensoresNivel()
{
  const int pines[3] = {PIN_NIVEL_1, PIN_NIVEL_2, PIN_NIVEL_3};
  for (int i = 0; i < 3; i++)
  {
    bool lectura = digitalRead(pines[i]); // LOW = vacio, HIGH = con liquido
    if (lectura == LOW)
    {
      if (!candidatoVacioActivo[i])
      {
        candidatoVacioActivo[i] = true;
        tCandidatoVacio[i] = millis();
      }
      else if (!tanqueVacio[i] && millis() - tCandidatoVacio[i] >= TIEMPO_CONFIRMAR_VACIO_MS)
      {
        tanqueVacio[i] = true;
        logSerial("[NIVEL] Tanque " + String(i + 1) + " confirmado VACIO");
      }
    }
    else
    {
      candidatoVacioActivo[i] = false;
      if (tanqueVacio[i])
      {
        tanqueVacio[i] = false;
        logSerial("[NIVEL] Tanque " + String(i + 1) + " tiene liquido de nuevo");
      }
    }
  }
}

String mascaraDisponibilidad()
{
  String m = "";
  for (int i = 0; i < 3; i++)
    m += tanqueVacio[i] ? "0" : "1";
  return m;
}

// DISPENSADO
bool iniciarDispensado(const char *origen)
{
  if (estadoActual != PAGO_EXITOSO)
  {
    logSerial("[DISPENSAR] Ignorado (" + String(origen) + "): no hay pago exitoso pendiente");
    return false;
  }
  activarBomba(prodSel);
  tInicioDisp = millis();

  logSerial("[DISPENSAR] Iniciado desde: " + String(origen) + " | producto " + String(prodSel) +
            " | corte por tiempo: " + String(tiempoDispensadoDe(prodSel, volSel) / 1000.0) + "s");
  cambiarEstado(DISPENSANDO);
  return true;
}

void chequearBotonesBoquilla()
{
  bool actBoq1 = digitalRead(PIN_BTN_BOQ_1);
  bool actBoq2 = digitalRead(PIN_BTN_BOQ_2);
  bool actBoq3 = digitalRead(PIN_BTN_BOQ_3);
  unsigned long ahora = millis();

  if (ahora - tUltimoBoton > DEBOUNCE_MS)
  {
    if (actBoq1 == LOW && antBoq1 == HIGH && prodSel == 1)
    {
      iniciarDispensado("boquilla 1");
      tUltimoBoton = ahora;
    }
    else if (actBoq2 == LOW && antBoq2 == HIGH && prodSel == 2)
    {
      iniciarDispensado("boquilla 2");
      tUltimoBoton = ahora;
    }
    else if (actBoq3 == LOW && antBoq3 == HIGH && prodSel == 3)
    {
      iniciarDispensado("boquilla 3");
      tUltimoBoton = ahora;
    }
    else if ((actBoq1 == LOW && antBoq1 == HIGH) || (actBoq2 == LOW && antBoq2 == HIGH) || (actBoq3 == LOW && antBoq3 == HIGH))
    {
      logSerial("[BOQUILLA] Boton presionado pero no corresponde al producto pagado");
      tUltimoBoton = ahora;
    }
  }
  antBoq1 = actBoq1;
  antBoq2 = actBoq2;
  antBoq3 = actBoq3;
}

// Solo duerme en estados de espera, sin compra a mitad de camino
bool puedeDormir()
{
  return (estadoActual == REPOSO || estadoActual == QR_ESPERA || estadoActual == QR_CONTINUAR);
}

// Deep sleep real: apaga WiFi, CPU, todo. Al despertar el ESP32 arranca de
// cero (vuelve a setup(), estado REPOSO) - no retoma la transaccion anterior.
// Unica forma de despertar: boton fisico en GPIO13 (nivel LOW).
void entrarDeepSleep()
{
  logSerial("[SLEEP] Sin actividad, entrando a deep sleep. Despierta con el boton fisico.");
  enviarComandoHMI("EST:SLEEP");
  hmiSerial.flush();
  if (servidorActivo)
    detenerServidorWeb();
  apagarBombas();

  rtc_gpio_pullup_en((gpio_num_t)PIN_BOTON_DESPERTAR);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BOTON_DESPERTAR);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BOTON_DESPERTAR, 0); // despierta en LOW

  esp_deep_sleep_start(); // no vuelve de aca, reinicia desde setup()
}

// SERVIDOR WEB BAJO DEMANDA (solo con metodo QR activo)
void iniciarServidorWeb()
{
  if (servidorActivo)
    return;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  dnsServer.start(53, "*", AP_IP);
  server.begin();
  servidorActivo = true;
  logSerial("[WEB] SoftAP + servidor iniciados. SSID: " + String(AP_SSID) + " | IP: " + WiFi.softAPIP().toString());
}

void detenerServidorWeb()
{
  if (!servidorActivo)
    return;
  server.end();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  servidorActivo = false;
  logSerial("[WEB] SoftAP + servidor detenidos");
}

// PROTOCOLO UART HACIA EL HMI (Arduino Uno + TFT)
//
// ESP32 -> Arduino (una linea de texto por comando, sin ACK):
//   EST:REPOSO:<saldo>       (saldo 0 si no hay nada pendiente)
//   EST:QR_ESPERA
//   EST:QR_PAGANDO
//   EST:PAGO_EXITOSO:<prod>:<vol>:<costo>
//   EST:QR_CONTINUAR         (post-dispensado QR: seguir o cancelar)
//   EST:MONEDA_INGRESO:<saldo>
//   EST:SEL_PROD:<saldo>:<mascara 3 digitos, 1=disponible 0=vacio>
//   EST:SEL_VOL:<prod>:<saldo>
//   EST:CONFIRMACION:<prod>:<vol>:<costo>:<saldo>
//   EST:DISPENSANDO           (sin tiempo, corte fijo por producto en el ESP32)
//   EST:FINALIZADO:<costo>
//   EST:SLEEP                (deep sleep real por inactividad)
//   SALDO:<valor>            (refresco rapido de saldo sin cambiar de pantalla)
//
// El ESP32 tambien reenvia su estado actual cada 2s sin que cambie nada
// (heartbeat), para que si el HMI se reinicia solo, se resincronice solo.
//
// Arduino -> ESP32:
//   TOQUE:<1-5>               (boton tocado, el ESP32 interpreta segun su estado)
//   SYNC                      (el Arduino la manda al arrancar, pide el estado actual ya mismo)
void enviarComandoHMI(String cmd)
{
  hmiSerial.println(cmd);
  logSerial("[HMI TX] " + cmd);
}

void enviarEstadoHMI()
{
  switch (estadoActual)
  {
  case REPOSO:
    enviarComandoHMI("EST:REPOSO:" + String(saldo));
    break;
  case QR_ESPERA:
    enviarComandoHMI("EST:QR_ESPERA");
    break;
  case QR_PAGANDO:
    enviarComandoHMI("EST:QR_PAGANDO");
    break;
  case PAGO_EXITOSO:
    enviarComandoHMI("EST:PAGO_EXITOSO:" + String(prodSel) + ":" + String(volSel) + ":" + String(costoSel));
    break;
  case MONEDA_INGRESO:
    enviarComandoHMI("EST:MONEDA_INGRESO:" + String(saldo));
    break;
  case SEL_PROD:
    enviarComandoHMI("EST:SEL_PROD:" + String(saldo) + ":" + mascaraDisponibilidad());
    break;
  case SEL_VOL:
    enviarComandoHMI("EST:SEL_VOL:" + String(prodSel) + ":" + String(saldo));
    break;
  case CONFIRMACION:
    enviarComandoHMI("EST:CONFIRMACION:" + String(prodSel) + ":" + String(volSel) + ":" + String(costoSel) + ":" + String(saldo));
    break;
  case DISPENSANDO:
    enviarComandoHMI("EST:DISPENSANDO");
    break;
  case FINALIZADO:
    enviarComandoHMI("EST:FINALIZADO:" + String(costoSel));
    break;
  case QR_CONTINUAR:
    enviarComandoHMI("EST:QR_CONTINUAR");
    break;
  }
}

void leerSerialHMI()
{
  while (hmiSerial.available() > 0)
  {
    char c = hmiSerial.read();
    if (c == '\n')
    {
      bufferHMI.trim();
      if (bufferHMI.length() > 0)
      {
        logSerial("[HMI RX] " + bufferHMI);
        if (bufferHMI.startsWith("TOQUE:"))
        {
          int btn = bufferHMI.substring(6).toInt();
          manejarToqueHMI(btn);
        }
        else if (bufferHMI == "SYNC")
        {
          logSerial("[HMI] Pidio SYNC, reenviando estado actual");
          enviarEstadoHMI();
        }
      }
      bufferHMI = "";
    }
    else if (c != '\r')
    {
      bufferHMI += c;
    }
  }
}

// NAVEGACION (toques del HMI). btn = 1..4 segun cuadrante tocado en la pantalla.
void manejarToqueHMI(int btn)
{
  ultimaActividad = millis();

  switch (estadoActual)
  {
  case REPOSO:
    if (btn == 1)
    {
      metodoActivo = METODO_QR;
      iniciarServidorWeb();
      cambiarEstado(QR_ESPERA);
    }
    else if (btn == 2)
    {
      metodoActivo = METODO_MONEDA;
      cambiarEstado(MONEDA_INGRESO); // saldo persistido, no se resetea
    }
    break;

  case QR_ESPERA:
    if (btn == 4)
    { // cancelar
      detenerServidorWeb();
      metodoActivo = METODO_NINGUNO;
      cambiarEstado(REPOSO);
    }
    break;

  case MONEDA_INGRESO:
    if (btn == 4)
    {
      if (saldo == 0)
      {
        metodoActivo = METODO_NINGUNO;
        cambiarEstado(REPOSO);
      }
      else
      {
        cambiarEstado(SEL_PROD);
      }
    }
    else if (btn == 5)
    {
      metodoActivo = METODO_NINGUNO;
      cambiarEstado(REPOSO);
    }
    break;

  case SEL_PROD:
    if (btn >= 1 && btn <= 3)
    {
      if (tanqueVacio[btn - 1])
      {
        logSerial("[MENU] Producto " + String(btn) + " sin stock, ignorado");
      }
      else
      {
        prodSel = btn;
        cambiarEstado(SEL_VOL);
      }
    }
    else if (btn == 4)
    {
      cambiarEstado(MONEDA_INGRESO);
    }
    break;

  case SEL_VOL:
  {
    if (btn >= 1 && btn <= 3)
    {
      Producto *p = buscarProducto(prodSel);
      int costo = calcularCosto(p, cantidadesPermitidas[btn - 1]);
      if (saldo >= costo)
      {
        volSel = btn;
        costoSel = costo;
        cambiarEstado(CONFIRMACION);
      }
      else
      {
        logSerial("[MENU] Saldo insuficiente para esa cantidad (" + String(costo) + " Bs, saldo " + String(saldo) + " Bs)");
        enviarComandoHMI("EST:SALDO_INSUF");
        enviarEstadoHMI();
      }
    }
    else if (btn == 4)
    {
      cambiarEstado(SEL_PROD);
    }
    break;
  }

  case QR_CONTINUAR:
    if (btn == 1)
    { // seguir en QR, el wifi/servidor ya estan prendidos
      cambiarEstado(QR_ESPERA);
    }
    else if (btn == 4)
    { // cancelar: recien aca se apaga wifi/servidor
      detenerServidorWeb();
      metodoActivo = METODO_NINGUNO;
      cambiarEstado(REPOSO);
    }
    break;

  case CONFIRMACION:
    if (btn == 1)
    {
      saldo -= costoSel;
      guardarSaldo();
      logSerial("[MONEDA] Compra confirmada. Producto " + String(prodSel) + " | " + String(costoSel) + " Bs | saldo restante " + String(saldo));
      cambiarEstado(PAGO_EXITOSO);
    }
    else if (btn == 4)
    {
      cambiarEstado(SEL_VOL);
    }
    break;

  default:
    logSerial("[MENU] Toque ignorado, estado " + String(nombreEstado(estadoActual)) + " no escucha el panel");
    break;
  }
}

// ENDPOINTS WEB (metodo QR)
void handleGetProductos(AsyncWebServerRequest *request)
{
  JsonDocument doc;
  JsonArray arr = doc["productos"].to<JsonArray>();
  for (int i = 0; i < 3; i++)
  {
    JsonObject p = arr.add<JsonObject>();
    p["id"] = productos[i].id;
    p["nombre"] = productos[i].nombre;
    p["precioPorLitro"] = productos[i].precioPorLitro;
    p["disponible"] = !tanqueVacio[i];
  }
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

bool litrosValidos(float litros)
{
  for (int i = 0; i < 3; i++)
  {
    if (fabs(cantidadesPermitidas[i] - litros) < 0.001)
      return true;
  }
  return false;
}

void handlePagarBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  if (metodoActivo != METODO_QR)
  {
    logSerial("[WEB] /pagar rechazado (metodo activo: " + String(nombreMetodo(metodoActivo)) + ")");
    request->send(403, "application/json", "{\"status\":\"forbidden\",\"message\":\"Pago QR no disponible\",\"metodo\":\"" + String(nombreMetodo(metodoActivo)) + "\"}");
    return;
  }
  if (estadoActual != QR_ESPERA)
  {
    logSerial("[WEB] /pagar rechazado (sistema ocupado) -> IP: " + request->client()->remoteIP().toString());
    request->send(503, "application/json", "{\"status\":\"busy\",\"message\":\"Dispensador ocupado\"}");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, data, len))
  {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON invalido\"}");
    return;
  }

  int idProducto = doc["producto"] | 0;
  float litros = doc["litros"] | 0.0;
  Producto *p = buscarProducto(idProducto);

  if (p == nullptr || tanqueVacio[idProducto - 1])
  {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Producto no disponible\"}");
    return;
  }
  if (!litrosValidos(litros))
  {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cantidad invalida\"}");
    return;
  }

  prodSel = p->id;
  volSel = (fabs(litros - 0.5) < 0.01) ? 1 : (fabs(litros - 1.0) < 0.01) ? 2
                                                                         : 3;
  costoSel = calcularCosto(p, litros);
  tInicioPagoQR = millis();

  logSerial("[WEB] /pagar OK desde IP " + request->client()->remoteIP().toString() + " | producto " + p->nombre + " | " + String(litros) + "L | " + String(costoSel) + " Bs");
  cambiarEstado(QR_PAGANDO);

  JsonDocument resp;
  resp["status"] = "ok";
  resp["monto"] = costoSel;
  String out;
  serializeJson(resp, out);
  request->send(200, "application/json", out);
}

void handleGetEstado(AsyncWebServerRequest *request)
{
  JsonDocument doc;
  doc["metodo"] = nombreMetodo(metodoActivo);

  if (estadoActual == QR_PAGANDO)
  {
    doc["estado"] = "pagando";
    long restante = (long)(TIEMPO_PAGO_MS / 1000) - (long)((millis() - tInicioPagoQR) / 1000);
    doc["segundosRestantes"] = restante < 0 ? 0 : restante;
  }
  else if (estadoActual == PAGO_EXITOSO)
  {
    doc["estado"] = "pago_exitoso";
    doc["monto"] = costoSel;
    doc["producto"] = prodSel;
  }
  else if (estadoActual == DISPENSANDO)
  {
    doc["estado"] = "dispensando"; // sin tiempo: corta el sensor de flujo, no un timer
    doc["monto"] = costoSel;
  }
  else if (estadoActual == FINALIZADO)
  {
    doc["estado"] = "finalizado";
    doc["monto"] = costoSel;
  }
  else
  {
    doc["estado"] = "libre";
  }

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void handleCaptivePortal(AsyncWebServerRequest *request)
{
  request->redirect("/");
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED)
  {
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
             info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
             info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
    logSerial("[WIFI] Cliente conectado -> MAC: " + String(mac));
  }
  else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED)
  {
    logSerial("[WIFI] Cliente desconectado");
  }
}

// TRANSICIONES AUTOMATICAS POR TIEMPO
void actualizarEstadoTiempos()
{
  if (estadoActual == QR_PAGANDO)
  {
    if (millis() - tInicioPagoQR >= TIEMPO_PAGO_MS)
    {
      logSerial("[PAGO] Pago QR confirmado");
      cambiarEstado(PAGO_EXITOSO);
    }
  }
  else if (estadoActual == DISPENSANDO)
  {
    unsigned long transcurrido = millis() - tInicioDisp;
    if (transcurrido >= tiempoDispensadoDe(prodSel, volSel))
    {
      apagarBombas();
      logSerial("[DISPENSAR] Completado (por tiempo)");
      tInicioFin = millis();
      cambiarEstado(FINALIZADO);
    }
  }
  else if (estadoActual == FINALIZADO)
  {
    if (millis() - tInicioFin >= TIEMPO_FIN_MS)
    {
      prodSel = 0;
      volSel = 0;
      costoSel = 0;
      if (metodoActivo == METODO_QR)
      {
        cambiarEstado(QR_CONTINUAR); // wifi/servidor siguen prendidos, se decide en el HMI
      }
      else if (metodoActivo == METODO_MONEDA && saldo > 0)
      {
        cambiarEstado(MONEDA_INGRESO);
      }
      else
      {
        metodoActivo = METODO_NINGUNO;
        cambiarEstado(REPOSO);
      }
    }
  }

  // Bajo consumo: solo se apaga la pantalla en REPOSO, nunca a mitad de una compra
  if (puedeDormir() && millis() - ultimaActividad >= TIEMPO_INACTIVIDAD_MS)
  {
    entrarDeepSleep();
  }
}

// SETUP
void setup()
{
#if DEBUG
  Serial.begin(115200);
  delay(300);
#endif

  hmiSerial.begin(HMI_BAUDIOS, SERIAL_8N1, PIN_HMI_RX2, PIN_HMI_TX2);

  prefs.begin("dispensador", false);
  saldo = prefs.getInt("saldo", 0);
  logSerial("[NVS] Saldo recuperado: " + String(saldo) + " Bs");

  // saldo = 50; // Pon aquí la cantidad fija que quieras probar (ej: 50 Bs)
  // guardarSaldo(); // Guarda este valor en NVS para que coincida

  pinMode(PIN_PLC_BOMBA_1, OUTPUT);
  pinMode(PIN_PLC_BOMBA_2, OUTPUT);
  pinMode(PIN_PLC_BOMBA_3, OUTPUT);
  apagarBombas();

  pinMode(PIN_BTN_BOQ_1, INPUT_PULLUP);
  pinMode(PIN_BTN_BOQ_2, INPUT_PULLUP);
  pinMode(PIN_BTN_BOQ_3, INPUT_PULLUP);

  pinMode(PIN_NIVEL_1, INPUT_PULLUP);
  pinMode(PIN_NIVEL_2, INPUT_PULLUP);
  pinMode(PIN_NIVEL_3, INPUT_PULLUP);

  pinMode(PIN_BOTON_DESPERTAR, INPUT_PULLUP);

  pinMode(PIN_COIN, INPUT);
  pinMode(PIN_COIN_SET, OUTPUT);
  digitalWrite(PIN_COIN_SET, LOW); // inhibido al iniciar
  attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);

  pinMode(INPUT_AUX_1, INPUT); // reservado, sin uso por ahora
  pinMode(INPUT_AUX_2, INPUT); // reservado, sin uso por ahora

  if (!LittleFS.begin(true))
  {
    logSerial("[SETUP][ERROR] Fallo montando LittleFS");
  }
  else
  {
    logSerial("[SETUP] LittleFS montado OK");
  }

  WiFi.onEvent(onWiFiEvent);

  // Rutas del servidor web (se registran una sola vez; el AP/servidor
  // se prende y apaga aparte con iniciarServidorWeb()/detenerServidorWeb())
  server.on("/productos", HTTP_GET, handleGetProductos);
  server.on("/estado", HTTP_GET, handleGetEstado);
  server.on("/pagar", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, handlePagarBody);
  server.on("/generate_204", HTTP_GET, handleCaptivePortal);
  server.on("/gen_204", HTTP_GET, handleCaptivePortal);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);
  server.on("/library/test/success.html", HTTP_GET, handleCaptivePortal);
  server.on("/ncsi.txt", HTTP_GET, handleCaptivePortal);
  server.on("/connecttest.txt", HTTP_GET, handleCaptivePortal);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound(handleCaptivePortal);

  ultimaActividad = millis();
  actualizarMonederoHabilitado();

  logSerial("=== SISTEMA INICIADO ===");
  logSerial("Estado inicial: " + String(nombreEstado(estadoActual)));
  enviarEstadoHMI();
}

// LOOP PRINCIPAL
void loop()
{
  if (servidorActivo)
    dnsServer.processNextRequest();

  // Monedero
  if (hayPulsoMoneda && !conteoMonedaEnProceso)
  {
    conteoMonedaEnProceso = true;
    tInicioConteoMoneda = millis();
    digitalWrite(PIN_COIN_SET, LOW);
    logSerial("[MONEDERO] Moneda detectada -> SET bloqueado temporalmente");
  }
  if (conteoMonedaEnProceso && (millis() - tInicioConteoMoneda >= TIEMPO_CONTEO_MONEDA))
  {
    procesarMoneda();
  }

  leerSerialHMI();
  chequearBotonesBoquilla();
  chequearSensoresNivel();
  actualizarEstadoTiempos();

  // Heartbeat: reenvia el estado actual cada 2s, asi si el HMI se reinicia
  // solo (o al reves), se resincroniza sin que nadie tenga que hacer nada.
  if (millis() - tUltimoHeartbeat >= HEARTBEAT_MS)
  {
    tUltimoHeartbeat = millis();
    enviarEstadoHMI();
  }
}
