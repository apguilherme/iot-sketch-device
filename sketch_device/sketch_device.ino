// EXAMPLE USING AN ESP8266 LoLin BOARD 
// CONNECTED TO A POTENTIOMETER AND A BUZZER, AND THE BUILT_IN LED

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#define LED LED_BUILTIN
#define POT A0
#define BUZ 16 // D0 pin
#define REL 1 // D1 pin

String deviceId = ""; // get it from frontend devices table
String devicePwd = "***"; // get it from frontend devices table
String webhookEndpoint = "http://<YOUR WIFI IPv4 PREFERENCIAL ADDRESS>:3000/api/devices/devicecredentials";
const char* mqttServer = "<YOUR WIFI IPv4 PREFERENCIAL ADDRESS>"; // example: 192.168.15.89
const char* ssid = ""; // your wifi name. It does not connect to 5Ghz, must be 2.4Ghz
const char* password = "***"; // your wifi password

DynamicJsonDocument credentialsObj(2048);
WiFiClient wifiClient;
PubSubClient pubsubClient(wifiClient);
long lastReconnectionTry = 0;
long frequency = 10 * 1000; // time interval to send data in milliseconds (seconds * 1000)
long lastTimeSent = 0;

bool getBrokerCredentials();
void checkBrokerConnection();
void checkConnection();
bool reconnectBroker();
void processActuatorData();
void publishBroker();
void processIncomingData(String topic, String incoming);
void brokerCallback(char* topic, byte* payload, unsigned int length);
String buildTopic(String variableIndex);
void printDataSent(String topic, String payload);

void setup() {
  Serial.begin(9600);
  pinMode(LED, OUTPUT);
  pinMode(POT, INPUT);
  pinMode(BUZ, OUTPUT);
  pinMode(REL, OUTPUT);
  digitalWrite(LED, HIGH);
  digitalWrite(BUZ, LOW);
  digitalWrite(REL, HIGH);
  delay(5000);
  int count = 0;
  Serial.print(">>> Starting wifi connection: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
    count++;
    if (count > 10) {
      Serial.println(">>> Wifi disconnected. Restarting in 10 seconds...");
      count = 0;
      delay(10000);
      ESP.restart();
    }
  }
  Serial.println("\n>>> Wifi connected. Local IP: ");
  Serial.print(WiFi.localIP());
  pubsubClient.setCallback(brokerCallback); // function called to handle incoming data from broker
}

void loop() {
  checkConnection();
}

void checkConnection() {
  if(WiFi.status() != WL_CONNECTED) {
    Serial.println(">>> Wifi disconnected. Restarting in 10 seconds...");
    delay(10000);
    ESP.restart();
  }
  if (!pubsubClient.connected()) {
    long now = millis();
    if (now - lastReconnectionTry > 5000) {
      lastReconnectionTry = millis();
      if (reconnectBroker()) {
        lastReconnectionTry = 0;
      }
    }
  }
  else {
    pubsubClient.loop();
    publishBroker();
  }
}

bool reconnectBroker() {
  if(!getBrokerCredentials()) {
    Serial.println(">>> Error getting MQTT broker credentials. Trying to reconnect in 5 seconds...");
    delay(5000);
    ESP.restart();
  }
  pubsubClient.setServer(mqttServer, 1883); // mqtt server
  Serial.println("\n>>> Starting MQTT broker connection...");
  const char* username = credentialsObj["credentials"]["username"];
  const char* password = credentialsObj["credentials"]["password"];
  String topic = credentialsObj["credentials"]["topic"];
  String clientId = "edge/" + topic + random(1, 9999);
  if (pubsubClient.connect(clientId.c_str(), username, password)) {
    Serial.println(">>> MQTT broker connection succeed.");
    pubsubClient.subscribe((topic + "+/actdata").c_str()); // subscribe: userId/deviceId/+/actdata
    return true;
  }
  else {
    Serial.println(">>> MQTT broker connection failed.");
    return false;
  }
}

bool getBrokerCredentials() {
  Serial.println("\n>>> Getting credentials for MQTT broker...");
  String body = "deviceID=" + deviceId + "&devicePwd=" + devicePwd;
  HTTPClient httpClient;
  httpClient.begin(wifiClient, webhookEndpoint);
  httpClient.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = httpClient.POST(body);
  if (code < 0 or code != 200) {
    Serial.println(">>> Error getting MQTT broker credentials. Response code: " + code);
    httpClient.end();
    return false;
  }
  if (code == 200) {
    String credentials = httpClient.getString();
    Serial.println(">>> MQTT broker credentials obtained:");
    deserializeJson(credentialsObj, credentials);
    serializeJsonPretty(credentialsObj, Serial);
    httpClient.end();
    delay(3000);
  }
  return true;
}

void publishBroker() {
  long now = millis();
  if ((now - lastTimeSent) >= frequency) {
    lastTimeSent = now;
    StaticJsonDocument<200> payloadJson;
    payloadJson["save"] = 1;
    String payload = "";
    String topic = "";
    
    // LED
    topic = buildTopic(0);
    payloadJson["value"] = digitalRead(LED) == 1 ? 0 : 1;
    serializeJson(payloadJson, payload);
    pubsubClient.publish(topic.c_str(), payload.c_str());
    printDataSent(topic, payload);
    payload = "";
    
    // POTENCIOMETRO
    topic = buildTopic(1);
    payloadJson["value"] = analogRead(POT);
    serializeJson(payloadJson, payload);
    pubsubClient.publish(topic.c_str(), payload.c_str());
    printDataSent(topic, payload);
    payload = "";
    
    // BUZZER
    topic = buildTopic(2);
    payloadJson["value"] = digitalRead(BUZ);
    serializeJson(payloadJson, payload);
    pubsubClient.publish(topic.c_str(), payload.c_str());
    printDataSent(topic, payload);
    payload = "";
    
    // RELE
    topic = buildTopic(3);
    payloadJson["value"] = digitalRead(REL) == 1 ? 0 : 1;
    serializeJson(payloadJson, payload);
    pubsubClient.publish(topic.c_str(), payload.c_str());
    printDataSent(topic, payload);
    payload = "";
    
  }
}

String buildTopic(int variableIndex) {
  String topicPrefix = credentialsObj["credentials"]["topic"];
  String topicPostfix = credentialsObj["credentials"]["variables"][variableIndex] + "/sdata";
  String topic = topicPrefix + topicPostfix; // publish: userId/deviceId/variable/sdata
  return topic;
}

void printDataSent(String topic, String payload) {
  Serial.print("DATA SENT");
  Serial.println(" | topic: " + topic + " | payload: " + payload);
}

void brokerCallback(char* topic, byte* payload, unsigned int length) {
  String incomingData = "";
  for (int i=0; i<length; i++) {
    incomingData = incomingData + (char) payload[i];
  }
  incomingData.trim();
  Serial.print("DATA RECEIVED");
  Serial.print(" | topic: ");
  Serial.print(topic);
  Serial.print(" | payload: ");
  Serial.print(incomingData);
  Serial.println();
  processIncomingData(topic, incomingData);
}

void processIncomingData(String topic, String incoming) {
  int lastBar = topic.lastIndexOf('/');
  int initialBar = topic.lastIndexOf('/', lastBar - 1);
  String variable = topic.substring(initialBar + 1, lastBar);
  DynamicJsonDocument incomingJson(256);
  deserializeJson(incomingJson, incoming);
  int value = incomingJson["value"];
  // LED
  if (variable == "led") {
    digitalWrite(LED, value == 1 ? 0 : 1); // LoLin ESP8266 led has 0 for HIGH and 1 for LOW
  }
  // BUZZER
  else if (variable == "buzzer") {
    digitalWrite(BUZ, value);
  }
  // RELE
  else if (variable == "rele") {
    digitalWrite(REL, value == 1 ? 0 : 1);
  }
}
