#include <Arduino.h>

const int PIN_SENSOR_FLUJO = 34;
const int PIN_BOMBA_1 = 17;

const int BTN_MEDIO_LITRO = 32;
const int BTN_UN_LITRO = 33;
const int BTN_DOS_LITROS = 25;

float FACTOR_PULSOS_POR_LITRO = 530.0;

volatile uint32_t contadorPulsos = 0; 
float litrosServidos = 0.0;
float metaLitros = 0.0;
uint32_t pulsosObjetivo = 0;
bool dispensando = false;
uint32_t ultimaLecturaSerial = 0;

void IRAM_ATTR contarPulsosISR()
{
  contadorPulsos++;
}

void setup()
{
  Serial.begin(115200);
  Serial.println("\n MODO DE CALIBRACION DE FLUJO ");

  pinMode(PIN_BOMBA_1, OUTPUT);
  digitalWrite(PIN_BOMBA_1, LOW);

  pinMode(BTN_MEDIO_LITRO, INPUT_PULLUP);
  pinMode(BTN_UN_LITRO, INPUT_PULLUP);
  pinMode(BTN_DOS_LITROS, INPUT_PULLUP);

  pinMode(PIN_SENSOR_FLUJO, INPUT);

  attachInterrupt(digitalPinToInterrupt(PIN_SENSOR_FLUJO), contarPulsosISR, FALLING);
}

void loop()
{
  if (!dispensando)
  {
    if (digitalRead(BTN_MEDIO_LITRO) == LOW)
    {
      metaLitros = 0.5;
      dispensando = true;
      delay(200); // Anti-rebote simple (Debounce)
    }
    else if (digitalRead(BTN_UN_LITRO) == LOW)
    {
      metaLitros = 1.0;
      dispensando = true;
      delay(200);
    }
    else if (digitalRead(BTN_DOS_LITROS) == LOW)
    {
      metaLitros = 2.0;
      dispensando = true;
      delay(200);
    }

    if (dispensando)
    {
      noInterrupts();
      contadorPulsos = 0;
      interrupts();
      litrosServidos = 0.0;
      pulsosObjetivo = (uint32_t)(metaLitros * FACTOR_PULSOS_POR_LITRO + 0.5);
      ultimaLecturaSerial = millis();
      digitalWrite(PIN_BOMBA_1, HIGH); // Encender bomba
      Serial.print("Iniciando dispensado. Meta: ");
      Serial.print(metaLitros, 1);
      Serial.print(" L - Pulsos objetivo: ");
      Serial.println(pulsosObjetivo);
    }
  }

  else
  {
    
    noInterrupts();
    uint32_t pulsosActuales = contadorPulsos;
    interrupts();

    litrosServidos = (float)pulsosActuales / FACTOR_PULSOS_POR_LITRO;

    // Mostrar continuamente el acumulado medido por el sensor.
    if (millis() - ultimaLecturaSerial >= 250)
    {
      Serial.print("Acumulado: ");
      Serial.print(litrosServidos * 1000.0, 1);
      Serial.print(" ml ( ");
      Serial.print(litrosServidos, 3);
      Serial.print(" L, pulsos: ");
      Serial.print(pulsosActuales);
      Serial.print("/");
      Serial.print(pulsosObjetivo);
      Serial.println(" pulsos )");
      ultimaLecturaSerial = millis();
    }

    // Comprobar si llegamos al objetivo de pulsos del boton.
    if (pulsosActuales >= pulsosObjetivo)
    {
      digitalWrite(PIN_BOMBA_1, LOW); // Apagar bomba
      dispensando = false;
      Serial.println("----------------------------------");
      Serial.println("DISPENSADO COMPLETADO");
      Serial.print("Pulsos totales registrados: ");
      Serial.println(pulsosActuales);
      Serial.print("Volumen medido: ");
      Serial.print(litrosServidos * 1000.0, 0);
      Serial.print(" ml ( ");
      Serial.print(litrosServidos, 3);
      Serial.println(" L )");
      Serial.println("Mide con una probeta/jarra el líquido real.");
      Serial.println("----------------------------------\n");
    }
  }
}
