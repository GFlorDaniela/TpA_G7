#include "Device.h"

Device::Device(int w, int h, int reset, int pinDHT, int modelo): _display(w, h, &Wire, reset), 
                                                                 _sensor(pinDHT, modelo)
{

}

void Device::begin() {
    _sensor.begin();
    _display.begin(0x3c, true);
    _display.setTextSize(1);
    _display.setTextColor(SH110X_WHITE);
    _display.clearDisplay();
}

void Device::dibujarPixel(int x, int y) {
    _display.drawPixel(x, y, SH110X_WHITE);
    _display.display();
}

void Device::showDisplay(String txt, int x, int y) {
    _display.setCursor(x, y);
    _display.print(txt);
    _display.display();
}

float Device::readTemp() {
    return _sensor.readTemperature();
}

float Device::readHum() {
    return _sensor.readHumidity();
}

void Device::dibujarSol() {
    _display.drawLine(95, 6, 95, 9, SH110X_WHITE);
    _display.drawLine(93, 10, 97, 10, SH110X_WHITE);
    _display.drawLine(87, 8, 90, 11, SH110X_WHITE);
    _display.drawLine(100, 11, 103, 8, SH110X_WHITE);
    _display.drawLine(90, 13, 92, 11, SH110X_WHITE);
    _display.drawLine(98, 11, 100, 13, SH110X_WHITE);
    _display.drawLine(89, 14, 89, 18, SH110X_WHITE);
    _display.drawLine(85, 16, 88, 16, SH110X_WHITE);
    _display.drawLine(95, 6, 95, 9, SH110X_WHITE);
    _display.drawLine(101, 14, 101, 18, SH110X_WHITE);
    _display.drawLine(102, 16, 105, 16, SH110X_WHITE);
    _display.drawLine(90, 19, 92, 21, SH110X_WHITE);
    _display.drawLine(98, 21, 100, 19, SH110X_WHITE);
    _display.drawLine(87, 24, 90, 21, SH110X_WHITE);
    _display.drawLine(100, 21, 103, 24, SH110X_WHITE);
    _display.drawLine(93, 22, 97, 22, SH110X_WHITE);
    _display.drawLine(95, 23, 95, 26, SH110X_WHITE);
    _display.drawRect(92, 13, 7, 7, SH110X_WHITE);
    _display.fillRect(92, 13, 7, 7, SH110X_WHITE);
    _display.drawLine(93, 12, 97, 12, SH110X_WHITE);
    _display.drawLine(93, 20, 97, 20, SH110X_WHITE);
    _display.drawLine(91, 14, 91, 18, SH110X_WHITE);
    _display.drawLine(99, 14, 99, 18, SH110X_WHITE);
    _display.display();
}

void Device::dibujarCheck(){
    _display.drawLine(85, 18, 93, 26, SH110X_WHITE);
    _display.drawLine(86, 17, 93, 24, SH110X_WHITE);
    _display.drawLine(87, 16, 93, 22, SH110X_WHITE);
    _display.drawLine(88, 15, 93, 20, SH110X_WHITE);
    _display.drawLine(86, 18, 93, 25, SH110X_WHITE);
    _display.drawLine(87, 17, 93, 23, SH110X_WHITE);
    _display.drawLine(88, 16, 93, 21, SH110X_WHITE);
    _display.drawLine(94, 25, 108, 11, SH110X_WHITE);
    _display.drawLine(94, 23, 107, 10, SH110X_WHITE);
    _display.drawLine(94, 22, 106, 10, SH110X_WHITE);
    _display.drawLine(94, 21, 106, 9, SH110X_WHITE);
    _display.drawLine(94, 20, 105, 9, SH110X_WHITE);
    _display.drawLine(94, 19, 105, 8, SH110X_WHITE);
    _display.display();
}


void Device::dibujarGota(float h) {
    _display.drawLine(90, 11, 95, 6, SH110X_WHITE);
    _display.drawLine(96, 7, 100, 11, SH110X_WHITE);
    _display.drawLine(89, 12, 89, 13, SH110X_WHITE);
    _display.drawLine(101, 12, 101, 13, SH110X_WHITE);
    _display.drawLine(88, 14, 88, 16, SH110X_WHITE);
    _display.drawLine(102, 14, 102, 16, SH110X_WHITE);
    _display.drawLine(87, 17, 87, 21, SH110X_WHITE);
    _display.drawLine(103, 17, 103, 21, SH110X_WHITE);
    _display.drawLine(88, 22, 88, 23, SH110X_WHITE);
    _display.drawLine(102, 22, 102, 23, SH110X_WHITE);
    _display.drawLine(89, 24, 90, 25, SH110X_WHITE);
    _display.drawLine(100, 25, 101, 24, SH110X_WHITE);
    _display.drawLine(91, 26, 99, 26, SH110X_WHITE);    
    if (h >= 5) { _display.drawLine(91, 25, 99, 25, SH110X_WHITE);
        if (h >= 10) { _display.drawLine(90, 24, 100, 24, SH110X_WHITE);
        if (h >= 15) { _display.drawLine(89, 23, 101, 23, SH110X_WHITE);
        if (h >= 20) { _display.drawLine(89, 22, 101, 22, SH110X_WHITE);
        if (h >= 25) { _display.drawLine(88, 21, 102, 21, SH110X_WHITE);
        if (h >= 30) { _display.drawLine(88, 20, 102, 20, SH110X_WHITE);
        if (h >= 35) { _display.drawLine(88, 19, 102, 19, SH110X_WHITE);
        if (h >= 40) { _display.drawLine(88, 18, 102, 18, SH110X_WHITE);
        if (h >= 45) { _display.drawLine(88, 17, 102, 17, SH110X_WHITE);
        if (h >= 50) { _display.drawLine(89, 16, 101, 16, SH110X_WHITE);
        if (h >= 55) { _display.drawLine(89, 15, 101, 15, SH110X_WHITE);
        if (h >= 60) { _display.drawLine(89, 14, 101, 14, SH110X_WHITE);
        if (h >= 65) { _display.drawLine(90, 13, 100, 13, SH110X_WHITE);
        if (h >= 70) { _display.drawLine(90, 12, 100, 12, SH110X_WHITE);
        if (h >= 75) { _display.drawLine(91, 11, 99, 11, SH110X_WHITE);
        if (h >= 80) { _display.drawLine(92, 10, 98, 10, SH110X_WHITE);
        if (h >= 85) { _display.drawLine(93, 9, 97, 9, SH110X_WHITE);
        if (h >= 90) { _display.drawLine(94, 8, 96, 8, SH110X_WHITE);
        if (h >= 95) { _display.drawPixel(95, 7, SH110X_WHITE);
        } }}}}}}}}}}}}}}}}}
    }
    
    _display.display();
}

void Device::clear() {
    _display.clearDisplay();
}
