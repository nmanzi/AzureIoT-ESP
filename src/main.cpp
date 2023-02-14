// Todos:
// - Read json data from the arduino via serial
// - For each key/value from arduino, send telemetry

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Additional sample headers
#include "iot_configs.h"

// Utility macros and defines
#define LED_PIN 2
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define ONE_HOUR_IN_SECS 3600
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_PACKET_SIZE 1024

// Translate iot_configs.h defines into variables used by the sample
static const char *ssid = IOT_CONFIG_WIFI_SSID;
static const char *password = IOT_CONFIG_WIFI_PASSWORD;
static const char *mqttHost = IOT_CONFIG_MQTT_SERVER;
static const char *mqttUser = IOT_CONFIG_MQTT_USER;
static const char *mqttPass = IOT_CONFIG_MQTT_PASS;
static const char *mqttCid = IOT_CONFIG_MQTT_CLIENTID;
static const char *sensorTopic = IOT_CONFIG_MQTT_SENSOR_TOPIC;
static const int port = 1883;

char willTopic[40];
const byte incomingBufferSize = 40;
char incomingBuffer[incomingBufferSize];
bool newData = false;

// Memory allocated for the sample's variables and structures.
static WiFiClient wifi_client;
static PubSubClient mqtt_client(wifi_client);
static unsigned long next_telemetry_send_time_ms = 0;

// Auxiliary functions

static void connectToWiFi()
{
  Serial.begin(115200);
  Serial.println();
  Serial.print("Connecting to WIFI SSID ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.print("WiFi connected, IP address: ");
  Serial.println(WiFi.localIP());
}

static void initializeTime()
{
  Serial.print("Setting time using SNTP");

  configTime(-5 * 3600, 0, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < 1510592825)
  {
    delay(500);
    Serial.print(".");
    now = time(NULL);
  }
  Serial.println("done!");
}

static char *getCurrentLocalTimeString()
{
  time_t now = time(NULL);
  return ctime(&now);
}

static void printCurrentTime()
{
  Serial.print("Current time: ");
  Serial.print(getCurrentLocalTimeString());
}

void receivedCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

static void initializeClients()
{
  mqtt_client.setServer(mqttHost, port);
  mqtt_client.setCallback(receivedCallback);
}

static int connectToMqtt()
{
  mqtt_client.setBufferSize(MQTT_PACKET_SIZE);

  while (!mqtt_client.connected())
  {
    Serial.print("MQTT connecting ... ");

    if (mqtt_client.connect(mqttCid, mqttUser, mqttPass,
                            willTopic, 0, false, "offline"))
    {
      Serial.println("connected.");
    }
    else
    {
      Serial.print("failed, status code =");
      Serial.print(mqtt_client.state());
      Serial.println(". Trying again in 5 seconds.");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }

  // Useful for when we want to perform actions
  // mqtt_client.subscribe(AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC);

  // We're online
  mqtt_client.publish(willTopic, "online", false);

  return 0;
}

static void establishConnection()
{
  connectToWiFi();
  initializeTime();
  printCurrentTime();
  initializeClients();
  connectToMqtt();
  digitalWrite(LED_PIN, LOW);
}

static void sendTelemetry(const char *topic, const char *payload)
{
  char destinationTopic[40];
  sprintf(destinationTopic, "%s/%s", sensorTopic, topic);
  digitalWrite(LED_PIN, HIGH);
  Serial.print(millis());
  Serial.printf(" Sending telemetry to %s . . . ", topic);
  mqtt_client.publish(destinationTopic, payload, false);
  Serial.println("OK");
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

// Read telemetry via serial
void receiveTelemetry()
{
  static byte ndx = 0;
  char endMarker = '\0'; // Read til we get a NULL
  char rc;

  while (Serial.available() > 0 && newData == false)
  {
    rc = Serial.read();

    if (rc != endMarker)
    {
      incomingBuffer[ndx] = rc;
      ndx++;
      if (ndx >= incomingBufferSize)
      {
        ndx = incomingBufferSize - 1;
      }
    }
    else
    {
      incomingBuffer[ndx] = '\0'; // terminate the string
      ndx = 0;
      newData = true;
    }
  }

  if (newData) {
    // We have some telemetry in JSON format
    // so send it as-is
    Serial.println(incomingBuffer);
    StaticJsonDocument<40> doc;
    DeserializationError error = deserializeJson(doc, incomingBuffer);
    // Test if parsing succeeds.
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      newData = false;
      return;
    }
    sendTelemetry(doc["sensor"], doc["value"]);
    newData = false;
  }
}

// Arduino setup and loop main functions.

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  sprintf(willTopic, "%s/availability", sensorTopic);
  establishConnection();
}

void loop()
{
  // Check if connected, reconnect if needed.
  if (!mqtt_client.connected())
  {
    establishConnection();
  }
  
  // Process any incoming telemetry
  receiveTelemetry();

  // MQTT loop must be called to process Device-to-Cloud and Cloud-to-Device.
  mqtt_client.loop();
  delay(500);
}