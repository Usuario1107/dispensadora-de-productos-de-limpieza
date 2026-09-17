#include <Arduino.h>
#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

void setup() {
  Serial.begin(115200);
}

void loop() {
  // Convierte la lectura cruda a grados Celsius
  float temp_celsius = (temprature_sens_read() - 32) / 1.8;
  Serial.print("Temperatura del chip: ");
  Serial.print(temp_celsius);
  Serial.println(" C");
  delay(5000);
}
