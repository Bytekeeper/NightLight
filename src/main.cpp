#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <NTPClient.h>
#include <PIR.h>
#include <Timezone.h>
#include "credentials.h"
#include "lightsensor.hpp"

static const uint8_t RED = D2;
static const uint8_t GREEN = D5;
static const uint8_t BLUE = D1;
const int changeSpeed = 3;
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

unsigned long currentMillis;
unsigned long nextUpdate = 0;

struct RGB
{
  int red;
  int green;
  int blue;
};

// Config mode
unsigned long manualColorEnd = 0;

unsigned long autoColorEnd = 0;
bool autoRetriggerTransitionPhase = false;

RGB dayColor = {0, 0, 0};
RGB transitionColor = {1023, 1023, 1023};
RGB nightColor = {511, 0, 0};
RGB color = {0, 0, 0};
RGB targetColor = {0, 0, 0};

void callback(char *topic, byte *payload, unsigned int length)
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

  if (!strcmp(topic, NIGHTLIGHT_TOPIC))
  {
    nightColor.red = red << 2;
    nightColor.green = green << 2;
    nightColor.blue = blue << 2;

    color = nightColor;
  }
}

void setup()
{
  // put your setup code here, to run once:
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(D1, OUTPUT);
  pinMode(D2, OUTPUT);
  pinMode(D5, OUTPUT);

  analogWrite(BLUE, 0);
  analogWrite(GREEN, 0);
  analogWrite(RED, 0);

  ntpClient.setUpdateInterval(600000);
  WiFi.hostname("Bedlight2");
  WiFi.begin(ssid, wifi_password);
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

  server.on("/status", [] {
    String out = "<html><head><body>";
    if (client.connected())
      out += "MQTT connected";
    else
      out += "MQTT <em>not</em> connected";
    out += "<br>";
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
  }
}

void loop()
{
  ArduinoOTA.handle();
  currentMillis = millis();
  if (!WiFi.isConnected())
    ESP.restart();
  if (!client.connected())
    mqttConnect();
  client.loop();
  server.handleClient();
  motion1.loop();
  motion2.loop();
  ntpClient.update();
  lightSens.loop();

  if (currentMillis > nextUpdate)
  {
    if (color.red + color.green + color.blue < 200 && lightSens.getValue() > 50)
    {
      autoColorEnd = 0;
    }
    else if (motion1.getState() == 1 || motion2.getState() == 1)
    {
      long secsToday = elapsedSecsToday(now());
      if (secsToday > nightFrom || secsToday < nightUntil)
      {
        autoColorEnd = currentMillis + 30000L;
        targetColor = nightColor;
      }
      else if (secsToday < dayFrom || secsToday > dayUntil || lightSens.getValue() < 20 || autoRetriggerTransitionPhase)
      {
        autoColorEnd = currentMillis + 10000L;
        targetColor = transitionColor;
        autoRetriggerTransitionPhase |= lightSens.getValue() < 20;
      }
      else
      {
        autoColorEnd = currentMillis + 10000L;
        targetColor = dayColor;
      }
    } else 
    {
      autoRetriggerTransitionPhase = false;
    }

    nextUpdate = max(nextUpdate + 20, currentMillis);
    if (currentMillis > autoColorEnd)
    {
      targetColor = {0, 0, 0};
    }
    if (currentMillis > manualColorEnd)
    {
      color.red = constrain(targetColor.red, color.red - changeSpeed, color.red + changeSpeed);
      color.green = constrain(targetColor.green, color.green - changeSpeed, color.green + changeSpeed);
      color.blue = constrain(targetColor.blue, color.blue - changeSpeed, color.blue + changeSpeed);
    }
    analogWrite(RED, color.red);
    analogWrite(GREEN, color.green);
    analogWrite(BLUE, color.blue);
    time_t epochSecond = ntpClient.getEpochTime();
    setTime(CE.toLocal(epochSecond));
  }
  delay(0);
}