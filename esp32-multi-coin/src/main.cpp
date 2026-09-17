// DISPENSADOR DE PRODUCTOS
// ESP32 - Selector HX-916 - LCD 20x4 I2C

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// PINES MONEDERO
#define PIN_COIN      18
#define PIN_SET       19

// TIEMPOS
#define TIEMPO_CONTEO 1000  // no cambiar, tiempo para procesar moneda

// PULSOS POR MONEDA
#define PULSOS_1BS    2
#define PULSOS_2BS    4
#define PULSOS_5BS    7

// LCD - direccion por defecto, cambiar a 0x3F si no funciona
#define LCD_DIRECCION  0x27
#define LCD_COLUMNAS   20
#define LCD_FILAS      4

LiquidCrystal_I2C lcd(LCD_DIRECCION, LCD_COLUMNAS, LCD_FILAS);

// VARIABLES MONEDERO
volatile int contadorPulsos      = 0;
volatile bool primerPulso        = false;
bool conteoActivo                = false;
unsigned long tiempoInicioConteo = 0;
int saldo                        = 0;

// DEFINICION DE FUNCIONES
// -- LECTURA DE MONEDA --
void IRAM_ATTR ISR_Moneda();
int  identificarMoneda(int pulsos);
void procesarMoneda();
// -- LCD --
void mostrarInicio();
void mostrarMoneda(int valor, int pulsos);
// -------------------------

void setup() {
  Serial.begin(115200);

  // LCD
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  mostrarInicio();

  // Monedero
  pinMode(PIN_COIN, INPUT);
  // SET: logica invertida por transistor NPN
  // ESP32 HIGH = selector BLOQUEADO
  // ESP32 LOW  = selector HABILITADO
  pinMode(PIN_SET, OUTPUT);
  digitalWrite(PIN_SET, HIGH);  // inicia habilitado

  attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);

  Serial.println("Sistema listo. Inserte moneda...");
}

void loop() {
  // Detecta primer pulso e inicia conteo
  if (primerPulso && !conteoActivo) {
    conteoActivo       = true;
    tiempoInicioConteo = millis();
    digitalWrite(PIN_SET, LOW);  // bloquea selector
    Serial.println("SET: BLOQUEADO");
  }
  // Al cumplirse 1 segundo procesa moneda
  if (conteoActivo && (millis() - tiempoInicioConteo >= TIEMPO_CONTEO)) {
    procesarMoneda();
  }
}

// INICIO - LECTURA DE MONEDA

// Interrupcion - IRAM_ATTR es obligatorio en ESP32
// para que la ISR corra desde RAM y no desde flash
void IRAM_ATTR ISR_Moneda() {
  contadorPulsos++;
  primerPulso = true;
}

// Compara sumatoria de pulsos con valor de moneda
// Retorna valor en Bs o 0 si no reconoce
int identificarMoneda(int pulsos) {
  if (pulsos == PULSOS_1BS) return 1;
  if (pulsos == PULSOS_2BS) return 2;
  if (pulsos == PULSOS_5BS) return 5;
  return 0;
}

// Desactiva ISR, lee total acumulado,
// resetea variables, habilita selector,
// reactiva ISR y acumula saldo
void procesarMoneda() {
  detachInterrupt(digitalPinToInterrupt(PIN_COIN));
  int pulsos     = contadorPulsos;
  contadorPulsos = 0;
  conteoActivo   = false;
  primerPulso    = false;
  digitalWrite(PIN_SET, HIGH);  // habilita selector
  Serial.println("SET: HABILITADO");
  attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);
  int valor = identificarMoneda(pulsos);
  if (valor > 0) {
    saldo += valor;
    mostrarMoneda(valor, pulsos);
    Serial.print("Pulsos: ");
    Serial.print(pulsos);
    Serial.print(" | Moneda: ");
    Serial.print(valor);
    Serial.print(" Bs | Saldo: ");
    Serial.print(saldo);
    Serial.println(" Bs");
  } else {
    Serial.print("Pulsos: ");
    Serial.print(pulsos);
    Serial.println(" | Moneda no reconocida");
  }
}

// FIN - LECTURA DE MONEDA

// INICIO - LCD

// Pantalla de bienvenida cuando saldo es 0
void mostrarInicio() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("   Bienvenido   ");
  lcd.setCursor(0, 1);
  lcd.print(" Inserte moneda ");
  lcd.setCursor(0, 2);
  lcd.print("  Saldo: 0 Bs   ");
}

// Muestra moneda recibida y saldo total
void mostrarMoneda(int valor, int pulsos) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Moneda recibida:");
  lcd.setCursor(0, 1);
  lcd.print("  ");
  lcd.print(valor);
  lcd.print(" Bs (");
  lcd.print(pulsos);
  lcd.print(" pulsos)  ");
  lcd.setCursor(0, 2);
  lcd.print("Saldo total:    ");
  lcd.setCursor(0, 3);
  lcd.print("  ");
  lcd.print(saldo);
  lcd.print(" Bs         ");
}

// FIN - LCD
