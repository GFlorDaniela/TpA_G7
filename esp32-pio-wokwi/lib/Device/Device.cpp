#include "Device.h"

Device ::Device(int w, int h, int reset, int pinOLED, int model) : _display(w, h, &Wire, reset),
                                                                             _sensor(pinOLED, model)
{
}

void Device::begin()
{
    _display.begin(0x3c, true);
    _display.setTextSize(1);
    _display.setTextColor(SH110X_WHITE);
    _sensor.begin();
}

void Device::showDisplay(const char* text)
{
    _display.clearDisplay();
    _display.setCursor(0, 0);
    _display.print(text);
    _display.display();
}
float Device::readTemp()
{
    return _sensor.readTemperature();
}

float Device::readHum()
{
    return _sensor.readHumidity();
}