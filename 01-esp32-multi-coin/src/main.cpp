#include <Arduino.h>

// PINES MONEDERO
#define PIN_COIN 18
#define PIN_SET 19

// TIEMPOS
#define TIEMPO_CONTEO 1000 // Tu tiempo original. Funciona perfecto si usas MEDIUM o SLOW en el monedero.

// PULSOS POR MONEDA
#define PULSOS_1BS 2
#define PULSOS_2BS 4
#define PULSOS_5BS 7

// VARIABLES MONEDERO
volatile int contadorPulsos = 0;
volatile bool primerPulso = false;
volatile unsigned long ultimoTiempoPulso = 0; // NUEVO: Filtro anti-ruido

bool conteoActivo = false;
unsigned long tiempoInicioConteo = 0;
int saldo = 0;

// DEFINICION DE FUNCIONES
void IRAM_ATTR ISR_Moneda();
int identificarMoneda(int pulsos);
void procesarMoneda();

void setup()
{
  Serial.begin(115200);

  // Monedero
  pinMode(PIN_COIN, INPUT);
  pinMode(PIN_SET, OUTPUT);
  digitalWrite(PIN_SET, HIGH); // inicia habilitado

  attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);

  Serial.println("Sistema listo. Inserte moneda...");
}

void loop()
{
  // Detecta primer pulso e inicia conteo
  if (primerPulso && !conteoActivo)
  {
    noInterrupts(); // Pausa interrupciones un microsegundo para evitar errores de memoria
    conteoActivo = true;
    tiempoInicioConteo = millis();
    primerPulso = false;
    interrupts(); // Reactiva interrupciones

    digitalWrite(PIN_SET, LOW); // TU LÓGICA: bloquea selector de inmediato
    Serial.println("SET: BLOQUEADO");
  }

  // Al cumplirse el tiempo (750ms) procesa moneda
  if (conteoActivo && (millis() - tiempoInicioConteo >= TIEMPO_CONTEO))
  {
    procesarMoneda();
  }
}

// INICIO - LECTURA DE MONEDA

void IRAM_ATTR ISR_Moneda()
{
  unsigned long tiempoActual = millis();

  // FILTRO ANTI-REBOTE: Si han pasado menos de 20ms, es un rebote falso, no lo cuenta.
  if (tiempoActual - ultimoTiempoPulso > 20)
  {
    contadorPulsos++;
    primerPulso = true;
    ultimoTiempoPulso = tiempoActual;
  }
}

int identificarMoneda(int pulsos)
{
  if (pulsos == PULSOS_1BS)
    return 1;
  if (pulsos == PULSOS_2BS)
    return 2;
  if (pulsos == PULSOS_5BS)
    return 5;
  return 0;
}

void procesarMoneda()
{
  detachInterrupt(digitalPinToInterrupt(PIN_COIN));

  int pulsos = contadorPulsos;
  contadorPulsos = 0;
  conteoActivo = false;
  primerPulso = false;

  int valor = identificarMoneda(pulsos);
  if (valor > 0)
  {
    saldo += valor;
    Serial.print("Pulsos reales: ");
    Serial.print(pulsos);
    Serial.print(" | Moneda: ");
    Serial.print(valor);
    Serial.print(" Bs | Saldo: ");
    Serial.print(saldo);
    Serial.println(" Bs");
  }
  else
  {
    Serial.print("Pulsos leidos: ");
    Serial.print(pulsos);
    Serial.println(" | Moneda no reconocida");
  }

  // Habilita selector DESPUÉS de procesar, para recibir la siguiente moneda
  digitalWrite(PIN_SET, HIGH);
  Serial.println("SET: HABILITADO");

  attachInterrupt(digitalPinToInterrupt(PIN_COIN), ISR_Moneda, FALLING);
}
