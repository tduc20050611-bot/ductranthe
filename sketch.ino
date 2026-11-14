#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <esp_arduino_version.h>
#include "mist_policy.h"

constexpr int DHT_PIN = 27, PUMP_PIN = 26, FAN_PIN = 25, MIST_PIN = 33;
constexpr char ROOT[] = "lab3/de2/vuon-dht22-5c8f14";
constexpr char BROKER[] = "broker.hivemq.com";
char telemetryTopic[80], stateTopic[80], availabilityTopic[80];
char pumpTopic[80], fanTopic[80], commandFilter[80];
DHT dht(DHT_PIN, DHT22);
WiFiClient socketClient;
PubSubClient mqtt(socketClient);

struct Sample { float temperature, humidity; bool valid; uint32_t uptime; };
struct Outputs { bool pump, fan; uint8_t mist; };
Outputs snapshotOutputs();
QueueHandle_t dataQueue, dataAlert;
SemaphoreHandle_t outputMutex;
Outputs outputs{};

Outputs snapshotOutputs() {
  xSemaphoreTake(outputMutex, portMAX_DELAY);
  Outputs copy = outputs;
  xSemaphoreGive(outputMutex);
  return copy;
}

void applyMist(uint8_t percent) {
  const uint32_t duty = (percent * 255u + 50u) / 100u;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(MIST_PIN, duty);
#else
  ledcWrite(0, duty);
#endif
  xSemaphoreTake(outputMutex, portMAX_DELAY);
  outputs.mist = percent;
  xSemaphoreGive(outputMutex);
  Serial.printf("[Core 1][MIST] %u%% (PWM=%lu/255)\n", percent, (unsigned long)duty);
}

void readTask(void *) {
  TickType_t previous = xTaskGetTickCount();
  for (;;) {
    float h = dht.readHumidity(), t = dht.readTemperature();
    Sample sample{t, h, isfinite(t) && isfinite(h) && h >= 0 && h <= 100, millis()};
    if (sample.valid) {
      Serial.printf("[Core %d][DHT22] T=%.1f C H=%.1f %%\n", xPortGetCoreID(), t, h);
    } else {
      Serial.println("[Core 1][DHT22] INVALID");
    }
    xQueueOverwrite(dataQueue, &sample);
    const float humidity = sample.valid ? h : NAN;
    xQueueOverwrite(dataAlert, &humidity);
    vTaskDelayUntil(&previous, pdMS_TO_TICKS(2500));
  }
}

void alertTask(void *) {
  MistCycle cycle;
  float humidity;
  for (;;) {
    if (xQueueReceive(dataAlert, &humidity, pdMS_TO_TICKS(50)) == pdTRUE) {
      cycle.updateHumidity(humidity, millis());
    }
    if (cycle.refresh(millis())) applyMist(cycle.percent());
  }
}

void commandReceived(char *topic, byte *payload, unsigned int size) {
  if (size != 1 || (payload[0] != '0' && payload[0] != '1')) return;
  const bool on = payload[0] == '1';
  xSemaphoreTake(outputMutex, portMAX_DELAY);
  if (strcmp(topic, pumpTopic) == 0) {
    digitalWrite(PUMP_PIN, on);
    outputs.pump = on;
    Serial.printf("[Core 0][MANUAL] PUMP=%d\n", on);
  } else if (strcmp(topic, fanTopic) == 0) {
    digitalWrite(FAN_PIN, on);
    outputs.fan = on;
    Serial.printf("[Core 0][MANUAL] FAN=%d\n", on);
  }
  xSemaphoreGive(outputMutex);
}

bool connectMqtt() {
  char clientId[40];
  snprintf(clientId, sizeof(clientId), "lab3-%08lx-%08lx",
           (unsigned long)ESP.getEfuseMac(), (unsigned long)esp_random());
  if (!mqtt.connect(clientId, availabilityTopic, 0, true, "offline")) {
    Serial.printf("[Core 0][MQTT] state=%d\n", mqtt.state());
    return false;
  }
  if (!mqtt.subscribe(commandFilter) || !mqtt.publish(availabilityTopic, "online", true)) {
    mqtt.disconnect();
    return false;
  }
  Serial.printf("[Core %d][MQTT] online\n", xPortGetCoreID());
  return true;
}

void transmitTask(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.begin("Wokwi-GUEST", "", 6);
  mqtt.setServer(BROKER, 1883);
  mqtt.setSocketTimeout(2);
  mqtt.setKeepAlive(15);
  mqtt.setCallback(commandReceived);
  Sample latest{};
  bool haveSample = false, pendingSample = false, pendingState = true;
  Outputs lastPublished{};
  uint32_t lastStateTime = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!mqtt.connected()) {
      if (!connectMqtt()) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        continue;
      }
      pendingState = true;
      pendingSample = haveSample;
    }
    if (!mqtt.loop()) continue;
    if (xQueueReceive(dataQueue, &latest, 0) == pdTRUE) {
      pendingSample = haveSample = true;
    }
    char json[160];
    if (pendingSample) {
      if (latest.valid) {
        snprintf(json, sizeof(json), "{\"temperature\":%.1f,\"humidity\":%.1f,\"uptime\":%lu,\"valid\":true}",
                 latest.temperature, latest.humidity, (unsigned long)latest.uptime);
      } else {
        snprintf(json, sizeof(json), "{\"temperature\":null,\"humidity\":null,\"uptime\":%lu,\"valid\":false}",
                 (unsigned long)latest.uptime);
      }
      if (mqtt.publish(telemetryTopic, json)) pendingSample = false;
    }
    const auto state = snapshotOutputs();
    const uint32_t now = millis();
    if (pendingState || state.pump != lastPublished.pump || state.fan != lastPublished.fan ||
        state.mist != lastPublished.mist || now - lastStateTime >= 2500) {
      snprintf(json, sizeof(json), "{\"pump\":%d,\"fan\":%d,\"mist\":%u}", state.pump, state.fan, state.mist);
      if (mqtt.publish(stateTopic, json)) {
        lastPublished = state;
        pendingState = false;
        lastStateTime = now;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!ledcAttach(MIST_PIN, 1000, 8)) {
    Serial.println("LEDC init failed");
    return;
  }
#else
  ledcSetup(0, 1000, 8);
  ledcAttachPin(MIST_PIN, 0);
#endif
  dht.begin();
  snprintf(telemetryTopic, sizeof(telemetryTopic), "%s/telemetry", ROOT);
  snprintf(stateTopic, sizeof(stateTopic), "%s/state", ROOT);
  snprintf(availabilityTopic, sizeof(availabilityTopic), "%s/availability", ROOT);
  snprintf(pumpTopic, sizeof(pumpTopic), "%s/cmd/pump", ROOT);
  snprintf(fanTopic, sizeof(fanTopic), "%s/cmd/fan", ROOT);
  snprintf(commandFilter, sizeof(commandFilter), "%s/cmd/+", ROOT);
  dataQueue = xQueueCreate(1, sizeof(Sample));
  dataAlert = xQueueCreate(1, sizeof(float));
  outputMutex = xSemaphoreCreateMutex();
  if (!dataQueue || !dataAlert || !outputMutex) {
    Serial.println("FreeRTOS init failed");
    return;
  }
  applyMist(0);
  xTaskCreatePinnedToCore(transmitTask, "Transmit", 8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(readTask, "ReadDHT22", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(alertTask, "MistAlert", 4096, nullptr, 3, nullptr, 1);
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
