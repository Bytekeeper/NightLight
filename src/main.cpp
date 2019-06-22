#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <PIR.h>
#include <Timezone.h>
#include <EEPROM.h>
#include "RGBControl.hpp"
#include "credentials.h"
#include "lightsensor.hpp"
#include "alarm.hpp"

static const int CONFIG_VERSION = 1;
static const uint8_t RED = D2;
static const uint8_t GREEN = D5;
static const uint8_t BLUE = D1;
static const short DEFAULT_SPEED = 3;
static const short ALARM_SPEED = 12;
static const RGB COLOR_OFF = {0, 0, 0};
const long dayFrom = 9 * 60 * 60;
const long dayUntil = 20 * 60 * 60;
const long nightFrom = 22 * 60 * 60;
const long nightUntil = 8 * 60 * 60;

TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120}; // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};   // Central European Standard Time
Timezone CE(CEST, CET);

void onMotionDetected(PirInfo *sender);

ESP8266WebServer server;
WiFiClient wifiClient;
PubSubClient client(wifiClient);
PIR motion1 = PIR(D6, onMotionDetected, "motion1");
PIR motion2 = PIR(D7, onMotionDetected, "motion2");
WiFiUDP ntpUDP;
NTPClient ntpClient(ntpUDP);
AnalogRead lightSens(A0);
RGBControl fader(RED, GREEN, BLUE);

unsigned long currentMillis;
unsigned long switchToIdleTime = 0;

struct Config
{
  short version = CONFIG_VERSION;
  RGB transitionColor = {1023, 1023, 1023};
  RGB nightColor = {511, 0, 0};
  RGB alarmColor = {1023, 1023, 0};
  Alarm alarm[10];
} config;

enum State
{
  IDLE,
  NIGHT_LIGHT,
  TRANSITION_LIGHT,
  DARK_LIGHT,
  ALARM_PULSE_OFF,
  ALARM_PULSE_ON
} state;

String lastTopic("None");

void callback(char *topic, byte *payload, unsigned int length)
{
  bool saveConfig = false;
  if (!strcmp(topic, NIGHTLIGHT_TOPIC))
  {
    char c[15];
    memcpy(c, payload, length);
    c[length] = '\0';
    char *tok = strtok(c, ",");
    int red = atoi(tok);
    tok = strtok(NULL, ",");
    int green = atoi(tok);
    tok = strtok(NULL, ",");
    int blue = atoi(tok);

    config.nightColor.red = red << 2;
    config.nightColor.green = green << 2;
    config.nightColor.blue = blue << 2;

    fader.fadeTo(config.nightColor, 0);
    saveConfig = true;
  }
  else if (length > 6 && !strcmp(topic, ALARM_SET_TOPIC))
  {
    int hour = (payload[2] - '0') * 10 + payload[3] - '0';
    int minute = (payload[5] - '0') * 10 + payload[6] - '0';
    config.alarm[0].setTime(payload[0] - '0', hour, minute);
    config.alarm[0].enable();
    saveConfig = true;
  }
  lastTopic = topic;
}

void setup()
{
  state = IDLE;

  // put your setup code here, to run once:
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(D1, OUTPUT);
  pinMode(D2, OUTPUT);
  pinMode(D5, OUTPUT);

  analogWrite(BLUE, 0);
  analogWrite(GREEN, 0);
  analogWrite(RED, 0);

  EEPROM.begin(sizeof(Config));
  Config tmp = Config();
  EEPROM.get(0, tmp);
  short storedVersion = tmp.version;
  if (storedVersion == CONFIG_VERSION)
  {
    config = tmp;
  }
  EEPROM.end();

  ntpClient.setUpdateInterval(600000);
  WiFi.hostname("Bedlight2");
  WiFi.begin(ssid, wifi_password);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
  }
  ArduinoOTA.setPassword(ota_password);
  ArduinoOTA.begin();

  server.on("/", [] {
    server.send(200, "text/html", "<html><body>TEST!</body></html>");
  });

  server.on("/toggle", [] {
    String out = "<html><body>";
    out += "triggering";
    out += "</body></html>";
    server.send(200, "text/html", out);
    digitalWrite(D1, HIGH);
    delay(500);
    digitalWrite(D1, LOW);
  });

  server.on("/live", [] {
    String out = "<html><head><meta http-equiv=\"refresh\" content=\"1\"></head><body>";
    out += "Motion detected: ";
    out += motion1.getState();
    out += "/";
    out += motion2.getState();
    out += "<br>A0:";
    out += lightSens.getValue();
    out += "<br>time:";
    out += hour();
    out += ":";
    out += minute();
    out += ":";
    out += second();
    out += "</body></html>";
    server.send(200, "text/html", out);
  });

  server.on("/status", [storedVersion] {
    String out = "<html><head><body>";
    if (client.connected())
      out += "MQTT connected";
    else
      out += "MQTT <em>not</em> connected";
    out += "<br>";
    out += "Config was ";
    if (storedVersion == CONFIG_VERSION)
      out += "loaded from EEPROM";
    else
    {
      out += "created from scratch, since stored version ";
      out += storedVersion;
      out += " does not match current version ";
      out += CONFIG_VERSION;
    }
    out += "<br>";
    for (Alarm &a : config.alarm)
    {
      out += a.toString();
      out += "<br>";
    }
    out += "last received topic: ";
    out += lastTopic;
    out += "<br>";
    out += fader.toString();
    out += "<br>time:";
    out += hour();
    out += ":";
    out += minute();
    out += ":";
    out += second();
    out += ", dow:";
    out += weekday();
    out += "</body></html>";
    server.send(200, "text/html", out);
  });

  server.begin();
  client.setServer(mqttServer, 1883);
  client.setCallback(callback);
}

void onMotionDetected(PirInfo *sender)
{
  if (!client.connected())
    return;
  String outTopic = String(BEDLIGHT_BASE_TOPIC) + sender->name;

  if (sender->state)
    client.publish(outTopic.c_str(), "1");
  else
    client.publish(outTopic.c_str(), "0");
}

void mqttConnect()
{
  static unsigned long lastTry = -10000;
  if (currentMillis <= lastTry + 10000)
    return;
  lastTry = currentMillis;
  String clientId = "ESP8266-" + String(random(0xffff), HEX);
  if (client.connect(clientId.c_str()))
  {
    client.subscribe(NIGHTLIGHT_TOPIC);
    client.subscribe(ALARM_SET_TOPIC);
  }
}

void darkLight()
{
  switchToIdleTime = currentMillis + 10000L;
  state = DARK_LIGHT;
}

void nightLight()
{
  switchToIdleTime = currentMillis + 30000L;
  state = NIGHT_LIGHT;
}

void transitionLight()
{
  switchToIdleTime = currentMillis + 10000L;
  state = TRANSITION_LIGHT;
}

void alarmState() {
  switchToIdleTime = currentMillis + 1000L;
  state = ALARM_PULSE_ON;
}

void onMotion()
{
  long secsToday = elapsedSecsToday(now());
  if (secsToday > nightFrom || secsToday < nightUntil)
  {
    nightLight();
  }
  else if (secsToday < dayFrom || secsToday > dayUntil)
  {
    transitionLight();
  }
  else if (lightSens.getValue() < 20)
  {
    darkLight();
  }
}

bool checkForAlarm(bool stopAlarms)
{
  if (stopAlarms)
  {
    for (Alarm &a : config.alarm)
    {
      a.stop();
      a.loop();
    }
    return false;
  }
  bool alarmActive = false;
  for (Alarm &a : config.alarm)
  {
    a.loop();
    alarmActive |= a.isActive();
  }
  return alarmActive;
}

void loop()
{
  ArduinoOTA.handle();
  currentMillis = millis();
  if (WiFi.isConnected())
  {
    bool mqttConnected = client.loop();
    if (!mqttConnected)
      mqttConnect();
    server.handleClient();
    ntpClient.update();
  }
  motion1.loop();
  motion2.loop();
  lightSens.loop();

  // Update clock
  time_t epochSecond = ntpClient.getEpochTime();
  setTime(CE.toLocal(epochSecond));

  bool motionDetected = motion1.getState() || motion2.getState();
  bool alarmActive = checkForAlarm(motionDetected);
  bool environmentIsLit = fader.isDark() && lightSens.getValue() > 30;

  if (currentMillis >= switchToIdleTime)
    state = IDLE;

  switch (state)
  {
  case IDLE:
    fader.fadeTo(COLOR_OFF, DEFAULT_SPEED);
    if (motionDetected && !environmentIsLit)
      onMotion();
    if (alarmActive)
      alarmState();
    break;
  case NIGHT_LIGHT:
    fader.fadeTo(config.nightColor, DEFAULT_SPEED);
    if (motionDetected)
      onMotion();
    if (alarmActive)
      alarmState();
    break;
  case TRANSITION_LIGHT:
    fader.fadeTo(config.transitionColor, DEFAULT_SPEED);
    if (motionDetected)
      onMotion();
    if (alarmActive)
      alarmState();
    break;
  case DARK_LIGHT:
    fader.fadeTo(config.transitionColor, DEFAULT_SPEED);
    if (motionDetected)
      darkLight();
    if (alarmActive)
      alarmState();
    break;
  case ALARM_PULSE_OFF:
    switchToIdleTime = currentMillis + 1000L;
    fader.fadeTo(COLOR_OFF, ALARM_SPEED);
    if (fader.reachedTargetColor())
      state = ALARM_PULSE_ON;
    if (!alarmActive)
      state = IDLE;
    break;
  case ALARM_PULSE_ON:
    switchToIdleTime = currentMillis + 1000L;
    fader.fadeTo(config.alarmColor, ALARM_SPEED);
    if (fader.reachedTargetColor())
      state = ALARM_PULSE_OFF;
    if (!alarmActive)
      state = IDLE;
    break;
  }

  fader.loop();
  delay(0);
}