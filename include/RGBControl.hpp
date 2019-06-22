#ifndef RGBCONTROL_H
#define RGBCONTROL_H
#include <Arduino.h>

struct RGB
{
    int red;
    int green;
    int blue;
};

bool operator==(const RGB &lhs, const RGB &rhs)
{
    return lhs.red == rhs.red && lhs.green == rhs.green && lhs.blue == rhs.blue;
}

class RGBControl
{
public:
    RGBControl(uint8_t redPin, uint8_t greenPin, uint8_t bluePin) : _redPin(redPin), _greenPin(greenPin), _bluePin(bluePin), fadeSpeed(3), color({0, 0, 0}), targetColor({0, 0, 0}), nextUpdate(0)
    {
        pinMode(redPin, OUTPUT);
        pinMode(greenPin, OUTPUT);
        pinMode(bluePin, OUTPUT);

        applyColor();
    }

    void fadeTo(RGB _targetColor, short speed)
    {
        fadeSpeed = speed;
        targetColor = _targetColor;
        if (fadeSpeed <= 0)
        {
            color = targetColor;
        }
    }

    void loop()
    {
        unsigned long time = millis();
        if (time >= nextUpdate)
        {
            color.red = constrain(targetColor.red, color.red - fadeSpeed, color.red + fadeSpeed);
            color.green = constrain(targetColor.green, color.green - fadeSpeed, color.green + fadeSpeed);
            color.blue = constrain(targetColor.blue, color.blue - fadeSpeed, color.blue + fadeSpeed);

            applyColor();
            nextUpdate = max(time, nextUpdate + 20);
        }
    }

    bool reachedTargetColor()
    {
        return targetColor == color;
    }

    bool isDark()
    {
        return color.red + color.green + color.blue < 200;
    }

    String toString()
    {
        String result = "r: ";
        result += color.red;
        result += ", g:";
        result += color.green;
        result += ", b:";
        result += color.blue;
        return result;
    }

private:
    uint8_t _redPin;
    uint8_t _greenPin;
    uint8_t _bluePin;
    short fadeSpeed;
    RGB color;
    RGB targetColor;
    unsigned long nextUpdate;

    void applyColor()
    {
        analogWrite(_redPin, color.red);
        analogWrite(_greenPin, color.green);
        analogWrite(_bluePin, color.blue);
    }
};

#endif