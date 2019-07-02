#ifndef ALARM_H
#define ALARM_H
#include <Arduino.h>
#include <TimeLib.h>

class Alarm
{
public:
    Alarm() : enabled(false) {}
    void setTime(uint8_t dayOfWeek, uint8_t hour, uint8_t minute)
    {
        _dayOfWeek = dayOfWeek;
        _hour = hour;
        _minute = minute;
    }

    int getAlarmSecond() const
    {
        return _hour * 60 * 60 + _minute * 60;
    }

    int dow() const { return _dayOfWeek; }

    void dow(int i) { _dayOfWeek = i; }

    void enable() { enabled = true; }

    void disable() { enabled = false; }

    bool isActive() const { return active; }

    bool isEnabled() const { return enabled; }

    void stop() { active = false; }

    void loop()
    {
        time_t time = now();
        int second = elapsedSecsToday(time);
        int alertSecond = _hour * 60 * 60 + _minute * 60;
        active = enabled && weekday(time) == _dayOfWeek &&
                 (alertSecond == second || (active && second > alertSecond && second < alertSecond + 15 * 60));
    }

    String toString() const
    {
        String result = String("Alarm ");
        result += "enabled: ";
        result += enabled;
        result += ", active: ";
        result += active;
        result += ", time: ";
        result += _hour;
        result += ":";
        result += _minute;
        result += ", DOW: ";
        result += _dayOfWeek;
        return result;
    }

private:
    bool enabled;
    bool active;
    uint8_t _hour;
    uint8_t _minute;
    uint8_t _dayOfWeek;
};

#endif
