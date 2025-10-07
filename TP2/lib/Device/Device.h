#ifndef DEVICE
#define DEVICE

#include <Arduino.h>
#include <Adafruit_SH110X.h>
#include <Wire.h>

class Device {
public:
    Device(int w, int h, int reset);
    void begin();
    void showDisplay(String txt, int x, int y);
    void clear();

private:
    Adafruit_SH1106G _display;
};

#endif
