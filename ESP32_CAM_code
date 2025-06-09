#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include <ArduinoJson.h>
#include <base64.h>

const char* ssid = "Villyan's Samsung S21 FE";
const char* password = "Villyan27";

AsyncWebServer server(80);

String toHexString(const uint8_t* data, size_t length) {
  String result = "";
  char hex[3];
  for (size_t i = 0; i < length; ++i) {
    sprintf(hex, "%02X", data[i]);
    result += hex;
  }
  return result;
}

void captureAndUpload(const String& id) {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://aol-iot-mr-default-rtdb.firebaseio.com/Report/" + id + ".json";
  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");

  String imageBase64 = base64::encode(fb->buf, fb->len);

  DynamicJsonDocument doc(16384);
  doc["image"] = imageBase64;

  String requestBody;
  serializeJson(doc, requestBody);

  int httpResponseCode = https.sendRequest("PATCH", requestBody);  // ✅ PATCH agar tidak overwrite
  Serial.print("Firebase response: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode > 0) {
    Serial.println(https.getString());
  } else {
    Serial.println(https.errorToString(httpResponseCode));
  }

  https.end();
  esp_camera_fb_return(fb);
}

void setupServer() {
  server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("id")) {
      String id = request->getParam("id")->value();
      request->send(200, "text/plain", "✅ Request received, capturing...");
      delay(100);  // allow response to go out first
      captureAndUpload(id);
    } else {
      request->send(400, "text/plain", "❌ Missing ID");
    }
  });

  server.begin();
}

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sscb_sda = 26;
  config.pin_sscb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_QVGA;  
  config.jpeg_quality = 15;              
  config.fb_count = 1;                  
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  setupCamera();
  setupServer();
}

void loop() {
}
