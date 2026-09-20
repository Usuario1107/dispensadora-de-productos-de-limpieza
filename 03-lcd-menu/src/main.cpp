#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// =============================================================================
// CONFIGURACIÓN Y MONITOREO SERIAL
// =============================================================================
const bool DEBUG = true;

void logSerial(String msg) {
  if (DEBUG) Serial.println("[SISTEMA] " + msg);
}

// =============================================================================
// MAPA DE PINES
// =============================================================================
const int PIN_COIN = 18;
const int PIN_COIN_SET = 19;

const int PIN_BTN_MENU_1 = 26;
const int PIN_BTN_MENU_2 = 27;
const int PIN_BTN_MENU_3 = 14;
const int PIN_BTN_MENU_4 = 13;

const int PIN_BTN_BOQ_1 = 32;
const int PIN_BTN_BOQ_2 = 33;
const int PIN_BTN_BOQ_3 = 25;

const int PIN_PLC_BOMBA_1 = 17;
const int PIN_PLC_BOMBA_2 = 16;
const int PIN_PLC_BOMBA_3 = 4;

// =============================================================================
// ESTRUCTURA Y CONFIGURACIÓN DINÁMICA DE PRODUCTOS
// =============================================================================
struct ProductoConfig {
  String nombre;
  int precioLitro;
};

// Configuración de productos (Nombre y Precio por Litro en Bs)
ProductoConfig productos[3] = {
  {"Producto 1", 4},
  {"Producto 2", 6},
  {"Producto 3", 8}
};

// =============================================================================
// CONFIGURACIÓN DE MONEDAS
// =============================================================================
#define TIEMPO_CONTEO 1000
#define PULSOS_1BS 2
#define PULSOS_2BS 4
#define PULSOS_5BS 7

volatile int pulsosContados = 0;
volatile bool hayPulso = false;
bool conteoEnProceso = false;
unsigned long tInicioConteo = 0;

// =============================================================================
// PANTALLA LCD 20x4
// =============================================================================
LiquidCrystal_I2C lcd(0x27, 20, 4);

// =============================================================================
// VARIABLES DEL SISTEMA
// =============================================================================
enum EstadoFSM {
  EST_REPOSO,
  EST_PRECIOS,
  EST_QR_ESPERA,
  EST_QR_PAGADO,
  EST_MONEDA_INGRESO,
  EST_SELEC_PROD,
  EST_SELEC_VOL,
  EST_CONFIRMACION,
  EST_DISPENSANDO
};

EstadoFSM estadoActual = EST_REPOSO;
EstadoFSM estadoAnterior = (EstadoFSM)-1;

int saldo = 0; 
int saldoAnt = -1;
int prodSel = 0; 
int volSel = 0; // 1: 0.5L, 2: 1L, 3: 2L
int costoSel = 0; 

// Control de Tiempos y Rebotes
unsigned long tUltimoBoton = 0;
const unsigned long DEBOUNCE_MS = 250;

bool antMenu1 = HIGH, antMenu2 = HIGH, antMenu3 = HIGH, antMenu4 = HIGH;
bool antBoq1 = HIGH, antBoq2 = HIGH, antBoq3 = HIGH;

unsigned long tInicioDisp = 0;
const unsigned long TIEMPO_DISP_MS = 10000; // 10 Segundos
int segRestantes = 10;
int segAnteriores = -1;

// =============================================================================
// PROTOTIPOS
// =============================================================================
void IRAM_ATTR ISR_Moneda();
int evaluarPulsos(int p);
void procesarMoneda();
void renderLCD(bool forzarCls);
void leerNavegacion(int btn);
void leerBoquillas(int boq);
void apagarBombas();
void activarBomba(int prod);
String pad20(String txt);
String getVolText(int vol);

// =============================================================================
// HELPERS FORMATO TEXTO
// =============================================================================
String pad20(String txt) {
  while (txt.length() < 20) {
    txt += " ";
  }
  return txt.substring(0, 20);
}

String getVolText(int vol) {
  if (vol == 1) return "0.5L";
  if (vol == 2) return "1L";
  if (vol == 3) return "2L";
  return "0L";
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  if (DEBUG) Serial.begin(115200);

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
  
  pinMode(PIN_COIN_SET, OUTPUT);
  digitalWrite(PIN_COIN_SET, LOW);

  apagarBombas();

  pinMode(PIN_COIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);

  logSerial("Iniciado correctamente. Esperando interacción...");
  renderLCD(true);
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================
void loop() {
  unsigned long tActual = millis();

  // 1. GESTIÓN DE MONEDERO
  if (hayPulso && !conteoEnProceso) {
    conteoEnProceso = true;
    tInicioConteo = millis();
    digitalWrite(PIN_COIN_SET, LOW);
    logSerial("Moneda detectada -> SET: BLOQUEADO");
  }

  if (conteoEnProceso && (millis() - tInicioConteo >= TIEMPO_CONTEO)) {
    procesarMoneda();
  }

  // 2. LECTURA DE BOTONES
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

  // 3. CONTEO DE DISPENSADO
  if (estadoActual == EST_DISPENSANDO) {
    unsigned long tTranscurrido = millis() - tInicioDisp;

    if (tTranscurrido < TIEMPO_DISP_MS) {
      segRestantes = 10 - (tTranscurrido / 1000);
      if (segRestantes != segAnteriores) {
        segAnteriores = segRestantes;
        renderLCD(false);
      }
    } else {
      apagarBombas();
      logSerial("Dispensado completado.");
      
      if (saldo > 0) estadoActual = EST_MONEDA_INGRESO;
      else estadoActual = EST_REPOSO;

      renderLCD(true);
    }
  }

  if (estadoActual != estadoAnterior) {
    renderLCD(true);
  }
}

// =============================================================================
// INTERRUPCIÓN Y MONEDERO
// =============================================================================
void IRAM_ATTR ISR_Moneda() {
  pulsosContados++;
  hayPulso = true;
}

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
    logSerial("Pulso ignorado: Estado no valido para cobro");
    return;
  }

  digitalWrite(PIN_COIN_SET, HIGH);
  attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);

  int valor = evaluarPulsos(p);
  if (valor > 0) {
    saldo += valor;
    logSerial("Moneda aceptada: " + String(valor) + " Bs | Saldo total: " + String(saldo) + " Bs");
    renderLCD(false);
  } else {
    logSerial("Error: Moneda no reconocida (Pulsos: " + String(p) + ")");
  }
}

// =============================================================================
// RENDERIZADO LCD 20x4 SIN ERRORES DE TEXTO
// =============================================================================
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
        lcd.setCursor(0, 0); lcd.print(pad20(productos[0].nombre + ": " + String(productos[0].precioLitro) + " Bs/L"));
        lcd.setCursor(0, 1); lcd.print(pad20(productos[1].nombre + ": " + String(productos[1].precioLitro) + " Bs/L"));
        lcd.setCursor(0, 2); lcd.print(pad20(productos[2].nombre + ": " + String(productos[2].precioLitro) + " Bs/L"));
        lcd.setCursor(0, 3); lcd.print("4 Inicio            ");
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
        // Lista vertical de los 3 productos + Saldo en Linea 0
        lcd.setCursor(0, 0); lcd.print("1 " + productos[0].nombre);
        lcd.setCursor(13, 0); lcd.print("S:    ");
        lcd.setCursor(0, 1); lcd.print(pad20("2 " + productos[1].nombre));
        lcd.setCursor(0, 2); lcd.print(pad20("3 " + productos[2].nombre));
        lcd.setCursor(0, 3); lcd.print("            4 Atras");
        break;

      case EST_SELEC_VOL: {
        digitalWrite(PIN_COIN_SET, HIGH);
        int pBase = productos[prodSel - 1].precioLitro;
        lcd.setCursor(0, 0); lcd.print("PROD P" + String(prodSel));
        lcd.setCursor(13, 0); lcd.print("S:    ");
        lcd.setCursor(0, 1); lcd.print(pad20("1:0.5L(" + String(pBase / 2) + "Bs) 2:1L(" + String(pBase) + "Bs)"));
        lcd.setCursor(0, 2); lcd.print(pad20("3:2L(" + String(pBase * 2) + "Bs)"));
        lcd.setCursor(0, 3); lcd.print("4 Atras             ");
        break;
      }

      case EST_CONFIRMACION:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print("CONFIRMAR");
        lcd.setCursor(13, 0); lcd.print("S:    ");
        lcd.setCursor(0, 1); lcd.print(pad20("Prod:P" + String(prodSel) + " | Vol:" + getVolText(volSel)));
        
        // Corrección explícita de "Costo:"
        lcd.setCursor(0, 2); lcd.print("Costo: "); 
        lcd.print(costoSel); 
        lcd.print(" Bs        ");
        
        lcd.setCursor(0, 3); lcd.print("1 Aceptar   4 Atras ");
        break;

      case EST_DISPENSANDO:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print("    DISPENSANDO     ");
        lcd.setCursor(0, 1); lcd.print(pad20("Sirviendo Prod P" + String(prodSel)));
        lcd.setCursor(0, 2); lcd.print("Por favor espere... ");
        lcd.setCursor(0, 3); lcd.print("Tiempo: 10s         ");
        break;

      case EST_QR_ESPERA:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print("    PAGO CON QR     ");
        lcd.setCursor(0, 1); lcd.print("Escanee con celular ");
        lcd.setCursor(0, 2); lcd.print("1 Simular Pago OK   ");
        lcd.setCursor(0, 3); lcd.print("4 Cancelar          ");
        break;

      case EST_QR_PAGADO:
        digitalWrite(PIN_COIN_SET, LOW);
        lcd.setCursor(0, 0); lcd.print("    PAGO EXITOSO    ");
        lcd.setCursor(0, 1); lcd.print("Puede dispensar     ");
        lcd.setCursor(0, 2); lcd.print(pad20("su producto P" + String(prodSel)));
        lcd.setCursor(0, 3); lcd.print(pad20("Presione boquilla " + String(prodSel)));
        break;
    }
  }

  // Refresco Dinámico de Saldo (Ubicación aislada en Columna 15, Fila 0)
  if (saldo != saldoAnt) {
    saldoAnt = saldo;

    if (estadoActual == EST_MONEDA_INGRESO || estadoActual == EST_SELEC_PROD || 
        estadoActual == EST_SELEC_VOL || estadoActual == EST_CONFIRMACION) {
      lcd.setCursor(15, 0);
      lcd.print(String(saldo) + "Bs ");
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

// =============================================================================
// NAVEGACIÓN
// =============================================================================
void leerNavegacion(int btn) {
  logSerial("Boton Menu presionado: [" + String(btn) + "]");

  switch (estadoActual) {
    case EST_REPOSO:
      if (btn == 1)      estadoActual = EST_QR_ESPERA;
      else if (btn == 2) estadoActual = EST_MONEDA_INGRESO;
      else if (btn == 3) estadoActual = EST_PRECIOS;
      break;

    case EST_PRECIOS:
      if (btn == 4) estadoActual = EST_REPOSO;
      break;

    case EST_QR_ESPERA:
      if (btn == 1) {
        prodSel = 1;
        logSerial("Pago QR simulado OK.");
        estadoActual = EST_QR_PAGADO;
      } else if (btn == 4) {
        estadoActual = EST_REPOSO;
      }
      break;

    case EST_MONEDA_INGRESO:
      if (btn == 4) {
        if (saldo == 0) estadoActual = EST_REPOSO;
        else            estadoActual = EST_SELEC_PROD;
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
      int pBase = productos[prodSel - 1].precioLitro;
      if (btn == 1)      { volSel = 1; costoSel = pBase / 2; }
      else if (btn == 2) { volSel = 2; costoSel = pBase; }
      else if (btn == 3) { volSel = 3; costoSel = pBase * 2; }

      if (btn >= 1 && btn <= 3) {
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
        logSerial("Compra confirmada.");
        estadoActual = EST_QR_PAGADO;
      } else if (btn == 4) {
        estadoActual = EST_SELEC_VOL;
      }
      break;

    default: break;
  }
}

// =============================================================================
// ACCIÓN DE BOQUILLAS
// =============================================================================
void leerBoquillas(int boq) {
  if (estadoActual == EST_QR_PAGADO) {
    logSerial("Boquilla pulsada: [" + String(boq) + "]");
    if (boq == prodSel) {
      activarBomba(boq);
      segRestantes = 10;
      segAnteriores = -1;
      tInicioDisp = millis();
      estadoActual = EST_DISPENSANDO;
      logSerial("Bombeando 10 segundos.");
    } else {
      logSerial("Error: Debe ser boquilla " + String(prodSel));
    }
  }
}

// =============================================================================
// SALIDAS AL PLC
// =============================================================================
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
