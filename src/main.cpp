#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <PIR.h>
#include <Timezone.h>
#include <EEPROM.h>
#include <TinyTemplateEngine.h>
#include <TinyTemplateEngineMemoryReader.h>
#include <WString.h>
#include <ArduinoJson.h>
#include "RGBControl.hpp"
#ifndef CI
#include "credentials.h"
#else
const char *ssid = "";
const char *wifi_password = "";
const char *ota_password = "";
const char *mqttServer = "";

const char *BEDLIGHT_BASE_TOPIC = "";
const char *NIGHTLIGHT_TOPIC = "";
const char *ALARM_SET_TOPIC = "";
const char *ALARM_STATE_TOPIC = "";
#endif
#include "lightsensor.hpp"
#include "alarm.hpp"
#include "html.h"

static const int CONFIG_VERSION = 1;
static const uint8_t RED = D2;
static const uint8_t GREEN = D5;
static const uint8_t BLUE = D1;
static const short SPEED_DEFAULT = 3;
static const short SPEED_FAST = 12;
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
String lastTopic("None");
unsigned long lastLit;

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

void saveConfig()
{
  EEPROM.begin(sizeof(Config));
  EEPROM.put(0, config);
  EEPROM.end();
}

void callback(char *topic, byte *payload, unsigned int length)
{
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
    saveConfig();

    fader.fadeTo(config.nightColor, 0);
  }
  else if (length > 6 && !strcmp(topic, ALARM_SET_TOPIC))
  {
    int hour = (payload[2] - '0') * 10 + payload[3] - '0';
    int minute = (payload[5] - '0') * 10 + payload[6] - '0';
    Alarm &a = config.alarm[payload[0] - '1'];
    a.setTime(payload[0] - '0', hour, minute);
    saveConfig();
  }
  else if (length >= 4 && !strcmp(topic, ALARM_STATE_TOPIC))
  {
    Alarm &a = config.alarm[payload[0] - '1'];
    if (payload[3] == 'f')
      a.disable();
    else if (payload[3] == 'n')
      a.enable();
    saveConfig();
  }
  lastTopic = topic;
}

void sendSensorData()
{
  StaticJsonDocument<300> doc;
  auto motionA = doc.createNestedObject();
  motionA["name"] = "Motion A";
  motionA["value"] = motion1.getState();

  auto motionB = doc.createNestedObject();
  motionB["name"] = "Motion B";
  motionB["value"] = motion2.getState();

  auto brightness = doc.createNestedObject();
  ;
  brightness["name"] = "Brightness";
  brightness["value"] = lightSens.getValue();

  auto timeSens = doc.createNestedObject();
  timeSens["name"] = "Time";
  String time;
  time += hour();
  time += ":";
  time += minute();
  time += ":";
  time += second();
  timeSens["value"] = time;

  auto faderSens = doc.createNestedObject();
  faderSens["name"] = "Fader";
  faderSens["value"] = fader.toString();

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void sendStatusData(short storedVersion)
{
  DynamicJsonDocument doc(2048);

  auto mqtt = doc.createNestedObject();
  mqtt["name"] = "MQTT";
  if (client.connected())
    mqtt["value"] = "Connected";
  else
    mqtt["value"] = "Not connected";

  auto configSrc = doc.createNestedObject();
  configSrc["name"] = "Configuration";
  if (storedVersion == CONFIG_VERSION)
    configSrc["value"] = "Saved";
  else
    configSrc["value"] = "New";

  for (const Alarm &a : config.alarm)
  {
    auto alarm = doc.createNestedObject();
    alarm["name"] = "Alarm";
    alarm["value"] = a.toString();
  }

  auto ltr = doc.createNestedObject();
  ltr["name"] = "Last received topic";
  ltr["value"] = lastTopic;

  auto llt = doc.createNestedObject();
  llt["name"] = "Room last lit";
  String lls;
  lls += (currentMillis - lastLit) / 1000;
  lls += "s ago";
  llt["value"] = lls;

  String out;
  serializeJson(doc, out);
  server.send(200, "text/html", out);
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
  int i = 0;
  for (Alarm &a: config.alarm) {
    a.dow(i);
    i++;
  }

  ntpClient.setUpdateInterval(600000);
  WiFi.hostname("NightLight");
  WiFi.begin(ssid, wifi_password);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  ArduinoOTA.setPassword(ota_password);
  ArduinoOTA.begin();

  server.on("/status.html", [] {
    server.send(200, "text/html", FPSTR(status_html));
  });

  server.on("/", [] {
    server.send(200, "text/html", FPSTR(index_html));
  });

  server.on("/set", [] {
    StaticJsonDocument<800> doc;
    deserializeJson(doc, server.arg("plain"));
    int i = 0;
    for (Alarm &a : config.alarm)
    {
      int second = doc[i]["second"];
      a.setTime(doc[i]["dow"], second / 60 / 60, second / 60 % 60);
      bool enabled = doc[i]["enabled"];
      if (enabled)
        a.enable();
      else
        a.disable();
      i++;
    }
    saveConfig();
    server.send(200);
  });

  server.on("/code.js", [] { server.send(200, "application/javascript", FPSTR(code_js)); });
  server.on("/settings.json", [] {
    StaticJsonDocument<800> doc;
    for (Alarm &a : config.alarm)
    {
      auto e = doc.createNestedObject();
      e["second"] = a.getAlarmSecond();
      e["dow"] = a.dow();
      e["enabled"] = a.isEnabled();
    }

    String out;
    serializeJson(doc, out);

    server.send(200, "application/json", out);
  });
  server.on("/sensors.json", sendSensorData);
  server.on("/status.json", [storedVersion] { sendStatusData(storedVersion); });

  server.on("/toggle", [] {
    String out = "<html><body>";
    out += "triggering";
    out += "</body></html>";
    server.send(200, "text/html", out);
    digitalWrite(D1, HIGH);
    delay(500);
    digitalWrite(D1, LOW);
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
    client.subscribe(ALARM_STATE_TOPIC);
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

void alarmState()
{
  switchToIdleTime = currentMillis + 1000L;
  state = ALARM_PULSE_ON;
}

void onMotion()
{
  long secsToday = elapsedSecsToday(now());
  if (secsToday < dayFrom || secsToday > dayUntil || currentMillis - lastLit < 1000)
  {
    transitionLight();
  }
  else if (secsToday > nightFrom || secsToday < nightUntil)
  {
    nightLight();
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
    digitalWrite(LED_BUILTIN, HIGH);
  }
  else
  {
    digitalWrite(LED_BUILTIN, LOW);
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
  if (environmentIsLit)
    lastLit = currentMillis;

  if (currentMillis >= switchToIdleTime)
    state = IDLE;

  switch (state)
  {
  case IDLE:
    fader.fadeTo(COLOR_OFF, SPEED_DEFAULT);
    if (motionDetected && !environmentIsLit)
      onMotion();
    if (alarmActive)
      alarmState();
    break;
  case NIGHT_LIGHT:
    fader.fadeTo(config.nightColor, SPEED_DEFAULT);
    if (motionDetected)
      onMotion();
    if (alarmActive)
      alarmState();
    break;
  case TRANSITION_LIGHT:
    fader.fadeTo(config.transitionColor, SPEED_FAST);
    if (motionDetected)
      onMotion();
    if (alarmActive)
      alarmState();
    break;
  case DARK_LIGHT:
    fader.fadeTo(config.transitionColor, SPEED_DEFAULT);
    if (motionDetected)
      darkLight();
    if (alarmActive)
      alarmState();
    break;
  case ALARM_PULSE_OFF:
    switchToIdleTime = currentMillis + 1000L;
    fader.fadeTo(COLOR_OFF, SPEED_FAST);
    if (fader.reachedTargetColor())
      state = ALARM_PULSE_ON;
    if (!alarmActive)
      state = IDLE;
    break;
  case ALARM_PULSE_ON:
    switchToIdleTime = currentMillis + 1000L;
    fader.fadeTo(config.alarmColor, SPEED_FAST);
    if (fader.reachedTargetColor())
      state = ALARM_PULSE_OFF;
    if (!alarmActive)
      state = IDLE;
    break;
  }

  fader.loop();
}