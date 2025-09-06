#include "Device.h"

const short LED = 23;
const short POTE = 32;

const short PIN_SENSOR = 14;
Device _device(128, 64, -1, PIN_SENSOR, DHT22);

void setup()
{
  Serial.begin(9600);
  pinMode(LED, OUTPUT);
  _device.begin();
  _device.showDisplay("DEVICE ON!");
}

void loop()
{
  // sensor:
  char buffer[64];
  float temp = _device.readTemp();
  float hum = _device.readHum();

  if (temp >= 35)
  {
    digitalWrite(LED, HIGH);
  }
  else
  {
    digitalWrite(LED, LOW);
  }
  int valor = analogRead(POTE);
  analogWrite(LED, map(valor, 0, 4095, 0, 255));
  sprintf(buffer, "Temp: %.1f C\nHum: %.1f %%\nVal: %d\nVoltios: %.2f", temp, hum, valor, valor * 3.3 / 4095);
  _device.showDisplay(buffer);
 
  delay(10);
}
