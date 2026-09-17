  
// DISPENSADOR DE DETERGENTES - ESP32
// Modulos integrados: Servidor Web (pago QR simulado), Monedero, LCD 20x4,
// Menu de navegacion, Boquillas de dispensado, Sensores de flujo (declarados)
  
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// DEBUG / SERIAL
#define DEBUG true

#if DEBUG
  #define debugPrint(x)    Serial.print(x)
  #define debugPrintln(x)  Serial.println(x)
#else
  #define debugPrint(x)
  #define debugPrintln(x)
#endif

void logSerial(String msg) {
  debugPrintln("[SISTEMA] " + msg);
}

// MAPA DE PINES (fijo del sistema -> MAYUSCULAS)
const int PIN_COIN        = 18;
const int PIN_COIN_SET    = 19;

const int PIN_BTN_MENU_1  = 26;
const int PIN_BTN_MENU_2  = 27;
const int PIN_BTN_MENU_3  = 14;
const int PIN_BTN_MENU_4  = 13;

const int PIN_BTN_BOQ_1   = 32;
const int PIN_BTN_BOQ_2   = 33;
const int PIN_BTN_BOQ_3   = 25;

const int PIN_PLC_BOMBA_1 = 17;
const int PIN_PLC_BOMBA_2 = 16;
const int PIN_PLC_BOMBA_3 = 4;

// Sensores de flujo YF-S201 (declarados, sin logica de corte todavia:
// aun no hay manguera conectada). Requieren pull-up externo de 10k a 3.3V.
const int PIN_FLUJO_1 = 35;
const int PIN_FLUJO_2 = 34;
const int PIN_FLUJO_3 = 36; // VP

// WIFI SOFTAP (editable)
const char* AP_SSID     = "ESP32-DEV";
const char* AP_PASSWORD = "1234567890";
IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_GATEWAY(192, 168, 4, 1);
IPAddress AP_SUBNET(255, 255, 255, 0);

// PRODUCTOS (fuente unica: la usan tanto el LCD como la web)
// precioPorLitro con decimales (ej. 4.50 Bs/L)
struct ProductoConfig {
  int id;
  String nombre;
  float precioPorLitro;
};

ProductoConfig productos[3] = {
  {1, "Producto 1", 10.00},
  {2, "Producto 2", 6.00},
  {3, "Producto 3", 8.00}
};

// Cantidades permitidas (litros) - indice 0=0.5L, 1=1L, 2=2L
const float cantidadesPermitidas[3] = {0.5, 1.0, 2.0};

ProductoConfig* buscarProducto(int id) {
  for (int i = 0; i < 3; i++) {
    if (productos[i].id == id) return &productos[i];
  }
  return nullptr;
}

// CONFIGURACION DE MONEDAS
#define TIEMPO_CONTEO 1000
#define PULSOS_1BS 2
#define PULSOS_2BS 4
#define PULSOS_5BS 7

volatile int pulsosContados = 0;
volatile bool hayPulso = false;
bool conteoEnProceso = false;
unsigned long tInicioConteo = 0;

// LCD 20x4
LiquidCrystal_I2C lcd(0x27, 20, 4);

// MAQUINA DE ESTADOS
enum EstadoFSM {
  EST_REPOSO,
  EST_PRECIOS,
  EST_QR_ESPERA,
  EST_QR_PAGANDO,     // simulacion de pago online (5s) - AGREGADO en la integracion
  EST_QR_PAGADO,
  EST_MONEDA_INGRESO,
  EST_SELEC_PROD,
  EST_SELEC_VOL,
  EST_CONFIRMACION,
  EST_DISPENSANDO,
  EST_FINALIZADO      // "retire su producto" - AGREGADO en la integracion
};

EstadoFSM estadoActual = EST_REPOSO;
EstadoFSM estadoAnterior = (EstadoFSM)-1;

// Metodo de pago activo (lo controla el panel/LCD; la web solo lo lee)
enum MetodoPago { METODO_NINGUNO, METODO_QR, METODO_MONEDA };
MetodoPago metodoActivo = METODO_NINGUNO;

const char* nombreMetodo(MetodoPago m) {
  switch (m) {
    case METODO_QR:     return "qr";
    case METODO_MONEDA: return "moneda";
    default:            return "ninguno";
  }
}

// Datos de la compra en curso (compartidos por ambos flujos: QR y Moneda)
float saldo = 0;
float saldoAnt = -1;
int   prodSel = 0;
int   volSel = 0;      // 1: 0.5L, 2: 1L, 3: 2L
float costoSel = 0;

// Tiempos de cada fase
const unsigned long TIEMPO_PAGO_MS   = 5000;   // simulacion de pago QR
const unsigned long TIEMPO_DISP_MS   = 10000;  // dispensado fijo (por ahora, sin sensor)
const unsigned long TIEMPO_FIN_MS    = 5000;   // pantalla "retire su producto"

unsigned long tInicioPagoQR = 0;
unsigned long tInicioDisp   = 0;
unsigned long tInicioFin    = 0;
int segRestantes  = 10;
int segAnteriores = -1;

// Debounce de botones (un solo timestamp global, un usuario a la vez)
unsigned long tUltimoBoton = 0;
const unsigned long DEBOUNCE_MS = 250;

bool antMenu1 = HIGH, antMenu2 = HIGH, antMenu3 = HIGH, antMenu4 = HIGH;
bool antBoq1 = HIGH, antBoq2 = HIGH, antBoq3 = HIGH;

// Sensores de flujo: solo conteo y log por ahora, sin corte por pulsos
volatile unsigned long pulsosFlujo1 = 0;
volatile unsigned long pulsosFlujo2 = 0;
volatile unsigned long pulsosFlujo3 = 0;

// OBJETOS DEL SERVIDOR WEB
DNSServer dnsServer;
AsyncWebServer server(80);

// PROTOTIPOS
void IRAM_ATTR ISR_Moneda();
void IRAM_ATTR ISR_Flujo1();
void IRAM_ATTR ISR_Flujo2();
void IRAM_ATTR ISR_Flujo3();
int evaluarPulsos(int p);
void procesarMoneda();
void renderLCD(bool forzarCls);
void leerNavegacion(int btn);
void leerBoquillas(int boq);
void apagarBombas();
void activarBomba(int prod);
String pad20(String txt);
String getVolText(int vol);
int volSelDeLitros(float litros);
void actualizarEstadoTiempos();

  
// HELPERS DE TEXTO
String pad20(String txt) {
  while (txt.length() < 20) txt += " ";
  return txt.substring(0, 20);
}

String getVolText(int vol) {
  if (vol == 1) return "0.5L";
  if (vol == 2) return "1L";
  if (vol == 3) return "2L";
  return "0L";
}

int volSelDeLitros(float litros) {
  if (fabs(litros - 0.5) < 0.01) return 1;
  if (fabs(litros - 1.0) < 0.01) return 2;
  if (fabs(litros - 2.0) < 0.01) return 3;
  return 0;
}

// SALIDAS AL PLC (BOMBAS)
void apagarBombas() {
  digitalWrite(PIN_PLC_BOMBA_1, LOW);
  digitalWrite(PIN_PLC_BOMBA_2, LOW);
  digitalWrite(PIN_PLC_BOMBA_3, LOW);
}

void activarBomba(int prod) {
  apagarBombas();
  if (prod == 1)      digitalWrite(PIN_PLC_BOMBA_1, HIGH);
  else if (prod == 2) digitalWrite(PIN_PLC_BOMBA_2, HIGH);
  else if (prod == 3) digitalWrite(PIN_PLC_BOMBA_3, HIGH);
}

// RENDERIZADO LCD 20x4
void renderLCD(bool forzarCls) {
  if (forzarCls || estadoActual != estadoAnterior) {
    lcd.clear();
    estadoAnterior = estadoActual;
    saldoAnt = -1;

    switch (estadoActual) {
      case EST_REPOSO:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print("       INICIO       ");
        lcd.setCursor(0, 1); lcd.print("1 Pago por QR       ");
        lcd.setCursor(0, 2); lcd.print("2 Pago por Monedas  ");
        lcd.setCursor(0, 3); lcd.print("3 Ver Precios       ");
        break;

      case EST_PRECIOS:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print(pad20(productos[0].nombre + ": " + String(productos[0].precioPorLitro, 2) + " Bs/L"));
        lcd.setCursor(0, 1); lcd.print(pad20(productos[1].nombre + ": " + String(productos[1].precioPorLitro, 2) + " Bs/L"));
        lcd.setCursor(0, 2); lcd.print(pad20(productos[2].nombre + ": " + String(productos[2].precioPorLitro, 2) + " Bs/L"));
        lcd.setCursor(0, 3); lcd.print("4 Inicio            ");
        break;

      case EST_QR_ESPERA:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print("    PAGO CON QR     ");
        lcd.setCursor(0, 1); lcd.print("Escanee con celular ");
        lcd.setCursor(0, 2); lcd.print("1 Simular Pago(DBG) ");
        lcd.setCursor(0, 3); lcd.print("4 Cancelar          ");
        break;

      case EST_QR_PAGANDO:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print("  PROCESANDO PAGO   ");
        lcd.setCursor(0, 1); lcd.print("Espere un momento...");
        break;

      case EST_QR_PAGADO:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print("    PAGO EXITOSO    ");
        lcd.setCursor(0, 1); lcd.print("Puede dispensar     ");
        lcd.setCursor(0, 2); lcd.print(pad20("su producto P" + String(prodSel)));
        lcd.setCursor(0, 3); lcd.print(pad20("Presione boquilla " + String(prodSel)));
        break;

      case EST_MONEDA_INGRESO:
        digitalWrite(PIN_COIN_SET, HIGH);
        lcd.setCursor(0, 0); lcd.print("MODO MONEDA  S:     ");
        lcd.setCursor(0, 1); lcd.print("Inserte monedas...  ");
        lcd.setCursor(0, 2); lcd.print("                    ");
        lcd.setCursor(0, 3);
        if (saldo == 0) lcd.print("4 Atras             ");
        else            lcd.print("4 Comprar           ");
        break;

      case EST_SELEC_PROD:
        digitalWrite(PIN_COIN_SET, HIGH);
        lcd.setCursor(0, 0); lcd.print("1 " + productos[0].nombre);
        lcd.setCursor(13, 0); lcd.print("S:    ");
        lcd.setCursor(0, 1); lcd.print(pad20("2 " + productos[1].nombre));
        lcd.setCursor(0, 2); lcd.print(pad20("3 " + productos[2].nombre));
        lcd.setCursor(0, 3); lcd.print("            4 Atras");
        break;

      case EST_SELEC_VOL: {
        digitalWrite(PIN_COIN_SET, HIGH);
        float pBase = productos[prodSel - 1].precioPorLitro;
        lcd.setCursor(0, 0); lcd.print("PROD P" + String(prodSel));
        lcd.setCursor(13, 0); lcd.print("S:    ");
        lcd.setCursor(0, 1); lcd.print(pad20("1:0.5L(" + String(pBase * 0.5, 2) + ") 2:1L(" + String(pBase, 2) + ")"));
        lcd.setCursor(0, 2); lcd.print(pad20("3:2L(" + String(pBase * 2.0, 2) + "Bs)"));
        lcd.setCursor(0, 3); lcd.print("4 Atras             ");
        break;
      }

      case EST_CONFIRMACION:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print("CONFIRMAR");
        lcd.setCursor(13, 0); lcd.print("S:    ");
        lcd.setCursor(0, 1); lcd.print(pad20("Prod:P" + String(prodSel) + " | Vol:" + getVolText(volSel)));
        lcd.setCursor(0, 2); lcd.print("Costo: ");
        lcd.print(String(costoSel, 2));
        lcd.print(" Bs    ");
        lcd.setCursor(0, 3); lcd.print("1 Aceptar   4 Atras ");
        break;

      case EST_DISPENSANDO:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print("    DISPENSANDO     ");
        lcd.setCursor(0, 1); lcd.print(pad20("Sirviendo Prod P" + String(prodSel)));
        lcd.setCursor(0, 2); lcd.print("Por favor espere... ");
        lcd.setCursor(0, 3); lcd.print("Tiempo: 10s         ");
        break;

      case EST_FINALIZADO:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print("       LISTO!       ");
        lcd.setCursor(0, 1); lcd.print("Retire su producto  ");
        lcd.setCursor(0, 2); lcd.print(pad20("Total: " + String(costoSel, 2) + " Bs"));
        break;
    }
  }

  // Refresco dinamico del saldo (sin recargar toda la pantalla)
  if (saldo != saldoAnt) {
    saldoAnt = saldo;
    if (estadoActual == EST_MONEDA_INGRESO || estadoActual == EST_SELEC_PROD ||
        estadoActual == EST_SELEC_VOL || estadoActual == EST_CONFIRMACION) {
      lcd.setCursor(15, 0);
      lcd.print(String(saldo, 1) + "Bs");
    }
    if (estadoActual == EST_MONEDA_INGRESO) {
      lcd.setCursor(0, 3);
      if (saldo > 0) lcd.print("4 Comprar           ");
      else           lcd.print("4 Atras             ");
    }
  }

  if (estadoActual == EST_DISPENSANDO) {
    lcd.setCursor(8, 3);
    if (segRestantes < 10) lcd.print("0");
    lcd.print(segRestantes); lcd.print("s ");
  }
}

// INTERRUPCIONES
void IRAM_ATTR ISR_Moneda() {
  pulsosContados++;
  hayPulso = true;
}

void IRAM_ATTR ISR_Flujo1() { pulsosFlujo1++; }
void IRAM_ATTR ISR_Flujo2() { pulsosFlujo2++; }
void IRAM_ATTR ISR_Flujo3() { pulsosFlujo3++; }

int evaluarPulsos(int p) {
  if (p == PULSOS_1BS) return 1;
  if (p == PULSOS_2BS) return 2;
  if (p == PULSOS_5BS) return 5;
  return 0;
}

void procesarMoneda() {
  detachInterrupt(digitalPinToInterrupt(PIN_COIN));
  int p = pulsosContados;
  pulsosContados = 0;
  conteoEnProceso = false;
  hayPulso = false;

  if (estadoActual != EST_MONEDA_INGRESO && estadoActual != EST_SELEC_PROD && estadoActual != EST_SELEC_VOL) {
    digitalWrite(PIN_COIN_SET, LOW);
    attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);
    logSerial("Pulso ignorado: estado no valido para cobro");
    return;
  }

  digitalWrite(PIN_COIN_SET, HIGH);
  attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);

  int valor = evaluarPulsos(p);
  if (valor > 0) {
    saldo += valor;
    logSerial("Moneda aceptada: " + String(valor) + " Bs | Saldo total: " + String(saldo, 2) + " Bs");
    renderLCD(false);
  } else {
    logSerial("Error: moneda no reconocida (pulsos: " + String(p) + ")");
  }
}

// NAVEGACION DEL MENU
void leerNavegacion(int btn) {
  logSerial("Boton menu presionado: [" + String(btn) + "]");

  switch (estadoActual) {
    case EST_REPOSO:
      if (btn == 1)      { metodoActivo = METODO_QR;     estadoActual = EST_QR_ESPERA; }
      else if (btn == 2) { metodoActivo = METODO_MONEDA; estadoActual = EST_MONEDA_INGRESO; saldo = 0; }
      else if (btn == 3) { estadoActual = EST_PRECIOS; }
      break;

    case EST_PRECIOS:
      if (btn == 4) estadoActual = EST_REPOSO;
      break;

    case EST_QR_ESPERA:
      // DEBUG: simula que ya llego un pago desde la web (Producto 1, 1L),
      // util para probar sin celular. El pago real siempre llega por /pagar.
      if (btn == 1) {
        prodSel  = 1;
        volSel   = 2; // 1L
        costoSel = productos[0].precioPorLitro * 1.0;
        logSerial("[DEBUG] Pago QR simulado desde el panel (Producto 1, 1L)");
        tInicioPagoQR = millis();
        estadoActual = EST_QR_PAGANDO;
      } else if (btn == 4) {
        metodoActivo = METODO_NINGUNO;
        estadoActual = EST_REPOSO;
      }
      break;

    case EST_MONEDA_INGRESO:
      if (btn == 4) {
        if (saldo == 0) { metodoActivo = METODO_NINGUNO; estadoActual = EST_REPOSO; }
        else            { estadoActual = EST_SELEC_PROD; }
      }
      break;

    case EST_SELEC_PROD:
      if (btn >= 1 && btn <= 3) {
        prodSel = btn;
        estadoActual = EST_SELEC_VOL;
      } else if (btn == 4) {
        estadoActual = EST_MONEDA_INGRESO;
      }
      break;

    case EST_SELEC_VOL: {
      float pBase = productos[prodSel - 1].precioPorLitro;
      if (btn >= 1 && btn <= 3) {
        volSel = btn;
        costoSel = pBase * cantidadesPermitidas[btn - 1];

        if (saldo >= costoSel) {
          estadoActual = EST_CONFIRMACION;
        } else {
          logSerial("Saldo insuficiente.");
          lcd.setCursor(0, 3); lcd.print("!Saldo Insuficiente!");
          delay(1200);
          renderLCD(true);
        }
      } else if (btn == 4) {
        estadoActual = EST_SELEC_PROD;
      }
      break;
    }

    case EST_CONFIRMACION:
      if (btn == 1) {
        saldo -= costoSel;
        logSerial("Compra confirmada (moneda). Producto P" + String(prodSel) + " | " + getVolText(volSel) + " | " + String(costoSel, 2) + " Bs");
        estadoActual = EST_QR_PAGADO; // reutiliza la misma pantalla "pago exitoso"
      } else if (btn == 4) {
        estadoActual = EST_SELEC_VOL;
      }
      break;

    default: break;
  }
}

// ACCION DE BOQUILLAS
void leerBoquillas(int boq) {
  if (estadoActual == EST_QR_PAGADO) {
    logSerial("Boquilla pulsada: [" + String(boq) + "]");
    if (boq == prodSel) {
      activarBomba(boq);
      pulsosFlujo1 = 0; pulsosFlujo2 = 0; pulsosFlujo3 = 0; // reinicia conteo para este dispensado
      segRestantes = 10;
      segAnteriores = -1;
      tInicioDisp = millis();
      estadoActual = EST_DISPENSANDO;
      logSerial("Bombeando 10 segundos (por tiempo, sin corte por sensor aun).");
    } else {
      logSerial("Error: debe ser boquilla " + String(prodSel));
    }
  }
}

// ACTUALIZACION DE ESTADOS POR TIEMPO (pago QR, dispensado, finalizado)
void actualizarEstadoTiempos() {
  if (estadoActual == EST_QR_PAGANDO) {
    if (millis() - tInicioPagoQR >= TIEMPO_PAGO_MS) {
      estadoActual = EST_QR_PAGADO;
      logSerial("Pago QR confirmado -> PAGO_EXITOSO");
    }
  } else if (estadoActual == EST_DISPENSANDO) {
    unsigned long transcurrido = millis() - tInicioDisp;
    if (transcurrido < TIEMPO_DISP_MS) {
      segRestantes = 10 - (transcurrido / 1000);
      if (segRestantes != segAnteriores) {
        segAnteriores = segRestantes;
        renderLCD(false);
        // Log de pulsos de flujo acumulados (documentacion para calibrar mas
        // adelante, todavia no se usa para cortar el bombeo)
        debugPrint("[FLUJO] P1:"); debugPrint(pulsosFlujo1);
        debugPrint(" P2:"); debugPrint(pulsosFlujo2);
        debugPrint(" P3:"); debugPrintln(pulsosFlujo3);
      }
    } else {
      apagarBombas();
      logSerial("Dispensado completado.");
      estadoActual = EST_FINALIZADO;
      tInicioFin = millis();
    }
  } else if (estadoActual == EST_FINALIZADO) {
    if (millis() - tInicioFin >= TIEMPO_FIN_MS) {
      if (saldo > 0) {
        estadoActual = EST_MONEDA_INGRESO;
      } else {
        metodoActivo = METODO_NINGUNO;
        estadoActual = EST_REPOSO;
      }
      prodSel = 0; volSel = 0; costoSel = 0;
    }
  }

  if (estadoActual != estadoAnterior) {
    renderLCD(true);
  }
}

// HANDLERS DEL SERVIDOR WEB
void handleGetProductos(AsyncWebServerRequest *request) {
  JsonDocument doc;
  JsonArray arr = doc["productos"].to<JsonArray>();
  for (int i = 0; i < 3; i++) {
    JsonObject p = arr.add<JsonObject>();
    p["id"] = productos[i].id;
    p["nombre"] = productos[i].nombre;
    p["precioPorLitro"] = productos[i].precioPorLitro;
  }
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

bool litrosValidos(float litros) {
  for (int i = 0; i < 3; i++) {
    if (fabs(cantidadesPermitidas[i] - litros) < 0.001) return true;
  }
  return false;
}

void handlePagarBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (metodoActivo != METODO_QR) {
    debugPrint("[PAGAR] Rechazado (metodo activo: ");
    debugPrint(nombreMetodo(metodoActivo));
    debugPrintln(")");
    JsonDocument resp;
    resp["status"] = "forbidden";
    resp["message"] = "El pago por QR no esta disponible en este momento";
    resp["metodo"] = nombreMetodo(metodoActivo);
    String out;
    serializeJson(resp, out);
    request->send(403, "application/json", out);
    return;
  }

  if (estadoActual != EST_QR_ESPERA) {
    debugPrint("[PAGAR] Rechazado (sistema ocupado) -> intento desde IP: ");
    debugPrintln(request->client()->remoteIP());
    JsonDocument resp;
    resp["status"] = "busy";
    resp["message"] = "Dispensador ocupado, espera un momento";
    String out;
    serializeJson(resp, out);
    request->send(503, "application/json", out);
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON invalido\"}");
    return;
  }

  int idProducto = doc["producto"] | 0;
  float litros = doc["litros"] | 0.0;

  ProductoConfig* p = buscarProducto(idProducto);
  if (p == nullptr) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Producto invalido\"}");
    return;
  }
  if (!litrosValidos(litros)) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cantidad invalida\"}");
    return;
  }

  prodSel  = p->id;
  volSel   = volSelDeLitros(litros);
  costoSel = litros * p->precioPorLitro;
  tInicioPagoQR = millis();
  estadoActual = EST_QR_PAGANDO;

  debugPrint("[PAGAR] Simulacion iniciada desde IP: ");
  debugPrint(request->client()->remoteIP());
  debugPrint(" | producto: ");
  debugPrint(p->nombre);
  debugPrint(" | litros: ");
  debugPrint(litros);
  debugPrint(" | monto: ");
  debugPrintln(costoSel);

  JsonDocument resp;
  resp["status"] = "ok";
  resp["monto"] = costoSel;
  resp["tiempoEstimadoSeg"] = TIEMPO_DISP_MS / 1000;
  String out;
  serializeJson(resp, out);
  request->send(200, "application/json", out);
}

void handleGetEstado(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["metodo"] = nombreMetodo(metodoActivo);

  if (estadoActual == EST_QR_PAGANDO) {
    doc["estado"] = "pagando";
    long restante = (long)(TIEMPO_PAGO_MS / 1000) - (long)((millis() - tInicioPagoQR) / 1000);
    if (restante < 0) restante = 0;
    doc["segundosRestantes"] = restante;
  } else if (estadoActual == EST_QR_PAGADO) {
    doc["estado"] = "pago_exitoso";
    doc["monto"] = costoSel;
    doc["producto"] = prodSel;
  } else if (estadoActual == EST_DISPENSANDO) {
    doc["estado"] = "dispensando";
    doc["segundosRestantes"] = segRestantes;
    doc["monto"] = costoSel;
  } else if (estadoActual == EST_FINALIZADO) {
    doc["estado"] = "finalizado";
    doc["monto"] = costoSel;
  } else {
    doc["estado"] = "libre";
  }

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

// [DEBUG] Simula el boton fisico de la boquilla desde la web, util para
// probar el flujo QR sin tener las boquillas conectadas todavia.
void handleSimularBoton(AsyncWebServerRequest *request) {
  bool ok = false;
  if (estadoActual == EST_QR_PAGADO) {
    activarBomba(prodSel);
    pulsosFlujo1 = 0; pulsosFlujo2 = 0; pulsosFlujo3 = 0;
    segRestantes = 10;
    segAnteriores = -1;
    tInicioDisp = millis();
    estadoActual = EST_DISPENSANDO;
    logSerial("[DEBUG] Dispensado simulado desde la web");
    ok = true;
  }
  JsonDocument resp;
  resp["status"] = ok ? "ok" : "error";
  resp["message"] = ok ? "Dispensado simulado iniciado" : "No hay un pago exitoso pendiente";
  String out;
  serializeJson(resp, out);
  request->send(ok ? 200 : 400, "application/json", out);
}

void handleCaptivePortal(AsyncWebServerRequest *request) {
  request->redirect("/");
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
      char mac[18];
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
        info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
        info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
        info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
      debugPrint("[WIFI] Cliente CONECTADO -> MAC: ");
      debugPrint(mac);
      debugPrint(" | clientes actuales: ");
      debugPrintln(WiFi.softAPgetStationNum());
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
      char mac[18];
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
        info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
        info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
        info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5]);
      debugPrint("[WIFI] Cliente DESCONECTADO -> MAC: ");
      debugPrint(mac);
      debugPrint(" | clientes actuales: ");
      debugPrintln(WiFi.softAPgetStationNum());
      break;
    }
    default:
      break;
  }
}

// SETUP
void setup() {
  #if DEBUG
    Serial.begin(115200);
    delay(300);
  #endif

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  pinMode(PIN_BTN_MENU_1, INPUT_PULLUP);
  pinMode(PIN_BTN_MENU_2, INPUT_PULLUP);
  pinMode(PIN_BTN_MENU_3, INPUT_PULLUP);
  pinMode(PIN_BTN_MENU_4, INPUT_PULLUP);

  pinMode(PIN_BTN_BOQ_1, INPUT_PULLUP);
  pinMode(PIN_BTN_BOQ_2, INPUT_PULLUP);
  pinMode(PIN_BTN_BOQ_3, INPUT_PULLUP);

  pinMode(PIN_PLC_BOMBA_1, OUTPUT);
  pinMode(PIN_PLC_BOMBA_2, OUTPUT);
  pinMode(PIN_PLC_BOMBA_3, OUTPUT);
  apagarBombas();

  pinMode(PIN_COIN_SET, OUTPUT);
  digitalWrite(PIN_COIN_SET, LOW); // inhibido al iniciar (Estado Reposo)

  pinMode(PIN_COIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);

  // Sensores de flujo: declarados y con interrupcion activa, solo para
  // empezar a ver pulsos en el Serial. Sin logica de corte todavia.
  pinMode(PIN_FLUJO_1, INPUT);
  pinMode(PIN_FLUJO_2, INPUT);
  pinMode(PIN_FLUJO_3, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_FLUJO_1), ISR_Flujo1, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_FLUJO_2), ISR_Flujo2, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_FLUJO_3), ISR_Flujo3, RISING);

  // --- LittleFS ---
  if (!LittleFS.begin(true)) {
    debugPrintln("[SETUP][ERROR] Fallo montando LittleFS");
  } else {
    debugPrintln("[SETUP] LittleFS montado OK");
  }

  // --- WiFi SoftAP ---
  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  debugPrint("[SETUP] SoftAP iniciado. SSID: ");
  debugPrint(AP_SSID);
  debugPrint(" | IP: ");
  debugPrintln(WiFi.softAPIP());

  // --- DNS (portal cautivo) ---
  dnsServer.start(53, "*", AP_IP);

  // --- Rutas API ---
  server.on("/productos", HTTP_GET, handleGetProductos);
  server.on("/estado", HTTP_GET, handleGetEstado);
  server.on("/pagar", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    nullptr,
    handlePagarBody
  );
  server.on("/simular-boton", HTTP_POST, handleSimularBoton); // DEBUG

  // --- Rutas de deteccion de portal cautivo ---
  server.on("/generate_204", HTTP_GET, handleCaptivePortal);
  server.on("/gen_204", HTTP_GET, handleCaptivePortal);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);
  server.on("/library/test/success.html", HTTP_GET, handleCaptivePortal);
  server.on("/ncsi.txt", HTTP_GET, handleCaptivePortal);
  server.on("/connecttest.txt", HTTP_GET, handleCaptivePortal);

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound(handleCaptivePortal);

  server.begin();

  logSerial("Sistema iniciado correctamente. Esperando interaccion...");
  renderLCD(true);
}

// LOOP PRINCIPAL
void loop() {
  dnsServer.processNextRequest();
  unsigned long tActual = millis();

  // 1. Gestion del monedero
  if (hayPulso && !conteoEnProceso) {
    conteoEnProceso = true;
    tInicioConteo = millis();
    digitalWrite(PIN_COIN_SET, LOW);
    logSerial("Moneda detectada -> SET: BLOQUEADO");
  }
  if (conteoEnProceso && (millis() - tInicioConteo >= TIEMPO_CONTEO)) {
    procesarMoneda();
  }

  // 2. Lectura de botones (menu + boquillas)
  bool actMenu1 = digitalRead(PIN_BTN_MENU_1);
  bool actMenu2 = digitalRead(PIN_BTN_MENU_2);
  bool actMenu3 = digitalRead(PIN_BTN_MENU_3);
  bool actMenu4 = digitalRead(PIN_BTN_MENU_4);
  bool actBoq1  = digitalRead(PIN_BTN_BOQ_1);
  bool actBoq2  = digitalRead(PIN_BTN_BOQ_2);
  bool actBoq3  = digitalRead(PIN_BTN_BOQ_3);

  if (tActual - tUltimoBoton > DEBOUNCE_MS) {
    if (actMenu1 == LOW && antMenu1 == HIGH)      { leerNavegacion(1); tUltimoBoton = tActual; }
    else if (actMenu2 == LOW && antMenu2 == HIGH) { leerNavegacion(2); tUltimoBoton = tActual; }
    else if (actMenu3 == LOW && antMenu3 == HIGH) { leerNavegacion(3); tUltimoBoton = tActual; }
    else if (actMenu4 == LOW && antMenu4 == HIGH) { leerNavegacion(4); tUltimoBoton = tActual; }

    if (actBoq1 == LOW && antBoq1 == HIGH)      { leerBoquillas(1); tUltimoBoton = tActual; }
    else if (actBoq2 == LOW && antBoq2 == HIGH) { leerBoquillas(2); tUltimoBoton = tActual; }
    else if (actBoq3 == LOW && antBoq3 == HIGH) { leerBoquillas(3); tUltimoBoton = tActual; }
  }

  antMenu1 = actMenu1; antMenu2 = actMenu2; antMenu3 = actMenu3; antMenu4 = actMenu4;
  antBoq1  = actBoq1;  antBoq2  = actBoq2;  antBoq3  = actBoq3;

  // 3. Transiciones automaticas por tiempo (pago QR, dispensado, finalizado)
  actualizarEstadoTiempos();
}
