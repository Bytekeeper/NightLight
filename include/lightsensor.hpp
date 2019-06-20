#ifndef __LIGHTSENSOR_H
#define __LIGHTSENSOR_H
#include <Arduino.h>

class AnalogRead
{
public:
    AnalogRead(uint8_t pin);
    void loop();
    int getValue();

private:
    uint8_t _pin;
    unsigned long updateInterval;
    unsigned long nextUpdate;
    int value;
};

AnalogRead::AnalogRead(uint8_t pin) : _pin(pin), updateInterval(1000L), nextUpdate(0) {}

void AnalogRead::loop()
{
    unsigned long time = millis();
    if (time > nextUpdate)
    {
        nextUpdate = max(nextUpdate + updateInterval, time);
        value = analogRead(_pin);
    }
}

int AnalogRead::getValue()
{
    return value;
}

#endif
