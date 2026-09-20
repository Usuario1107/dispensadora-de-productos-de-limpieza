#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

//  
// MODO DEBUG
// Poner en false para "produccion": elimina todos los Serial.print
// del binario compilado (no gasta tiempo ni memoria).
//  
#define DEBUG true

#if DEBUG
  #define debugPrint(x)    Serial.print(x)
  #define debugPrintln(x)  Serial.println(x)
#else
  #define debugPrint(x)
  #define debugPrintln(x)
#endif

//  
// CONFIGURACION WIFI (SoftAP) - editable
//  
const char* AP_SSID     = "ESP32-DEV";
const char* AP_PASSWORD = "1234567890";
IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_GATEWAY(192, 168, 4, 1);
IPAddress AP_SUBNET(255, 255, 255, 0);

//  
// PRODUCTOS (3 tipos de detergente) - editable
// id, nombre, precio por litro
//  
// tiempoDispensadoSeg: placeholder por producto (luego se reemplaza
// por sensor de flujo + interrupciones, puede variar segun la densidad
// o el caudal de cada detergente).
struct Producto {
  int id;
  const char* nombre;
  float precioPorLitro;
  unsigned long tiempoDispensadoSeg;
};

Producto productos[3] = {
  {1, "Detergente A", 5.00, 10},
  {2, "Detergente B", 7.00, 10},
  {3, "Detergente C", 10.00, 10}
};

// Cantidades permitidas (litros)
const float cantidadesPermitidas[3] = {0.5, 1.0, 2.0};

//  
// METODO DE PAGO ACTIVO (placeholder - viene del panel fisico/LCD)
// Por ahora es una variable fija que se cambia a mano y se
// recompila, hasta que se integre el modulo de botonera/LCD real.
//  
enum MetodoPago {
  METODO_NINGUNO,
  METODO_QR,
  METODO_MONEDA
};

MetodoPago metodoActivo = METODO_MONEDA; // <-- cambiar aca para probar (NINGUNO / QR / MONEDA)

const char* nombreMetodo(MetodoPago m) {
  switch (m) {
    case METODO_QR:     return "qr";
    case METODO_MONEDA: return "moneda";
    default:            return "ninguno";
  }
}

//  
// MAQUINA DE ESTADOS
//  
enum EstadoSistema {
  LIBRE,
  PAGANDO,        // simulacion de pago (5s fijos)
  PAGO_EXITOSO,   // esperando que se dispare el dispensado (web o boton fisico)
  DISPENSANDO,
  FINALIZADO
};

EstadoSistema estadoActual = LIBRE;
unsigned long inicioPago = 0;
unsigned long inicioDispensado = 0;
unsigned long inicioFinalizado = 0;
float montoActual = 0;
int productoActualId = 0;
float litrosActuales = 0;
unsigned long tiempoDispensadoActualSeg = 0; // tiempo del producto que se esta dispensando ahora

const unsigned long TIEMPO_PAGO_MS = 5000;       // duracion fija de la simulacion de pago
const unsigned long TIEMPO_FINALIZADO_MS = 5000; // cuanto se muestra "listo" antes de volver a LIBRE

//  
// BOTON FISICO (placeholder - pin definitivo pendiente)
//  
#define PIN_BOTON_DISPENSAR 0   // TODO: cambiar al pin real cuando se conecte el boton
bool ultimoEstadoBoton = HIGH;  // asumiendo pull-up (boton a GND)

//  
// OBJETOS GLOBALES
//  
DNSServer dnsServer;
AsyncWebServer server(80);

//  
// UTILIDADES
//  
Producto* buscarProducto(int id) {
  for (int i = 0; i < 3; i++) {
    if (productos[i].id == id) return &productos[i];
  }
  return nullptr;
}

bool litrosValidos(float litros) {
  for (int i = 0; i < 3; i++) {
    if (fabs(cantidadesPermitidas[i] - litros) < 0.001) return true;
  }
  return false;
}

// Dispara el dispensado real. Se llama desde la web (/dispensar)
// o desde el boton fisico. El primero que llegue gana.
bool iniciarDispensado(const char* origen) {
  if (estadoActual != PAGO_EXITOSO) {
    return false; // no corresponde dispensar en este momento
  }
  estadoActual = DISPENSANDO;
  inicioDispensado = millis();
  debugPrint("[DISPENSAR] Disparado desde: ");
  debugPrintln(origen);
  return true;
}

// Actualiza la maquina de estados (llamar en loop)
void actualizarEstado() {
  if (estadoActual == PAGANDO) {
    if (millis() - inicioPago >= TIEMPO_PAGO_MS) {
      estadoActual = PAGO_EXITOSO;
      debugPrintln("[ESTADO] Pago simulado OK -> PAGO_EXITOSO (esperando dispensado)");
    }
  } else if (estadoActual == DISPENSANDO) {
    if (millis() - inicioDispensado >= (tiempoDispensadoActualSeg * 1000UL)) {
      estadoActual = FINALIZADO;
      inicioFinalizado = millis();
      debugPrintln("[ESTADO] Dispensado completo -> FINALIZADO");
    }
  } else if (estadoActual == FINALIZADO) {
    if (millis() - inicioFinalizado >= TIEMPO_FINALIZADO_MS) {
      estadoActual = LIBRE;
      montoActual = 0;
      productoActualId = 0;
      litrosActuales = 0;
      debugPrintln("[ESTADO] FINALIZADO -> LIBRE (listo para siguiente cliente)");
    }
  }
}

// Lee el boton fisico (polling simple con deteccion de flanco).
// Placeholder: cuando se defina el pin real, esto ya funciona igual.
void chequearBotonFisico() {
  bool estadoBoton = digitalRead(PIN_BOTON_DISPENSAR);
  // Flanco de bajada (boton con pull-up, presionado = LOW)
  if (ultimoEstadoBoton == HIGH && estadoBoton == LOW) {
    if (iniciarDispensado("boton fisico")) {
      debugPrintln("[BOTON] Dispensado iniciado por boton fisico");
    } else {
      debugPrintln("[BOTON] Presionado pero no hay pago exitoso pendiente");
    }
  }
  ultimoEstadoBoton = estadoBoton;
}

//  
// HANDLERS DEL SERVIDOR
//  

// GET /productos -> lista de productos disponibles
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

// POST /pagar -> body JSON {producto, litros}
void handlePagarBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  // Si el metodo de pago activo no es QR, la pasarela web esta cerrada
  if (metodoActivo != METODO_QR) {
    debugPrint("[PAGAR] Rechazado (metodo activo no es QR, es: ");
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

  // Si el sistema esta ocupado (alguien ya pagando/pago-exitoso/dispensando), se ignora
  if (estadoActual != LIBRE) {
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
    debugPrintln("[PAGAR] Error parseando JSON");
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON invalido\"}");
    return;
  }

  int producto = doc["producto"] | 0;
  float litros = doc["litros"] | 0.0;

  Producto* p = buscarProducto(producto);
  if (p == nullptr) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Producto invalido\"}");
    return;
  }
  if (!litrosValidos(litros)) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Cantidad invalida\"}");
    return;
  }

  // Tomar el lock: arranca la SIMULACION de pago (no dispensa todavia)
  montoActual = litros * p->precioPorLitro;
  productoActualId = p->id;
  litrosActuales = litros;
  tiempoDispensadoActualSeg = p->tiempoDispensadoSeg;
  estadoActual = PAGANDO;
  inicioPago = millis();

  debugPrint("[PAGAR] Simulacion iniciada desde IP: ");
  debugPrint(request->client()->remoteIP());
  debugPrint(" | producto: ");
  debugPrint(p->nombre);
  debugPrint(" | litros: ");
  debugPrint(litros);
  debugPrint(" | monto: ");
  debugPrintln(montoActual);

  JsonDocument resp;
  resp["status"] = "ok";
  resp["monto"] = montoActual;
  resp["tiempoEstimadoSeg"] = tiempoDispensadoActualSeg;
  String out;
  serializeJson(resp, out);
  request->send(200, "application/json", out);
}

// GET /estado -> polling: en que estado esta el sistema
void handleGetEstado(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["metodo"] = nombreMetodo(metodoActivo);

  switch (estadoActual) {
    case LIBRE:
      doc["estado"] = "libre";
      break;
    case PAGANDO: {
      doc["estado"] = "pagando";
      unsigned long transcurrido = (millis() - inicioPago) / 1000;
      long restante = (long)(TIEMPO_PAGO_MS / 1000) - (long)transcurrido;
      if (restante < 0) restante = 0;
      doc["segundosRestantes"] = restante;
      break;
    }
    case PAGO_EXITOSO:
      doc["estado"] = "pago_exitoso";
      doc["monto"] = montoActual;
      doc["producto"] = productoActualId;
      break;
    case DISPENSANDO: {
      doc["estado"] = "dispensando";
      unsigned long transcurrido = (millis() - inicioDispensado) / 1000;
      long restante = (long)tiempoDispensadoActualSeg - (long)transcurrido;
      if (restante < 0) restante = 0;
      doc["segundosRestantes"] = restante;
      doc["monto"] = montoActual;
      break;
    }
    case FINALIZADO:
      doc["estado"] = "finalizado";
      doc["monto"] = montoActual;
      break;
  }

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

// Handler generico para portal cautivo: redirige todo a "/"
void handleCaptivePortal(AsyncWebServerRequest *request) {
  request->redirect("/");
}

//  
// [TEMPORAL / DEBUG] Simula el boton fisico de la boquilla.
// Quitar o dejar solo para pruebas cuando ya exista la botonera
// real de 3 botones (uno por producto) en las boquillas.
//  
void handleSimularBoton(AsyncWebServerRequest *request) {
  bool ok = iniciarDispensado("simular-boton (DEBUG)");
  JsonDocument resp;
  resp["status"] = ok ? "ok" : "error";
  resp["message"] = ok ? "Dispensado simulado iniciado" : "No hay un pago exitoso pendiente";
  String out;
  serializeJson(resp, out);
  request->send(ok ? 200 : 400, "application/json", out);
}

//  
// EVENTOS WIFI (log de conexiones/desconexiones de clientes al AP)
//  
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

//  
// SETUP
//  
void setup() {
  #if DEBUG
    Serial.begin(115200);
    delay(300);
    debugPrintln("\n[SETUP] Iniciando sistema...");
  #endif

  // --- Boton fisico (placeholder, pin definitivo pendiente) ---
  pinMode(PIN_BOTON_DISPENSAR, INPUT_PULLUP);

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

  // --- DNS Server (portal cautivo) ---
  dnsServer.start(53, "*", AP_IP);
  debugPrintln("[SETUP] DNS server activo (redirige todo a la IP del AP)");

  // --- Rutas API ---
  server.on("/productos", HTTP_GET, handleGetProductos);
  server.on("/estado", HTTP_GET, handleGetEstado);
  server.on("/pagar", HTTP_POST,
    [](AsyncWebServerRequest *request) { /* se maneja en el body callback */ },
    nullptr,
    handlePagarBody
  );
  server.on("/simular-boton", HTTP_POST, handleSimularBoton); // TEMPORAL: quitar con botonera real

  // --- Rutas tipicas de deteccion de portal cautivo (distintos SO) ---
  server.on("/generate_204", HTTP_GET, handleCaptivePortal);       // Android
  server.on("/gen_204", HTTP_GET, handleCaptivePortal);            // Android
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);// iOS/macOS
  server.on("/library/test/success.html", HTTP_GET, handleCaptivePortal); // iOS
  server.on("/ncsi.txt", HTTP_GET, handleCaptivePortal);           // Windows
  server.on("/connecttest.txt", HTTP_GET, handleCaptivePortal);    // Windows

  // --- Archivos estaticos (frontend) ---
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // --- Catch-all: cualquier otra ruta no reconocida -> portal cautivo ---
  server.onNotFound(handleCaptivePortal);

  server.begin();
  debugPrintln("[SETUP] Servidor web iniciado");
}

//  
// LOOP
//  
void loop() {
  dnsServer.processNextRequest();
  actualizarEstado();
  chequearBotonFisico();
}
