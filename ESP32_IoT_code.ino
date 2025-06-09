#include <Wire.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>
#include <ESP32Servo.h>
#include <Ticker.h>
#include <time.h>
#include <ArduinoJson.h>

//-----------------------------------------
// Pin Definitions
#define RST_PIN  22
#define SS_PIN   5
const int trigPin = 25;
const int echoPin = 26;
const int buzzPin = 13;
Servo myServo;
const int servoPin = 32;

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
const char* camIP = "192.168.220.139";  // IP statis ESP32-CAM

Ticker servoRefresher;
bool servoOpen = false;
bool servoAttached = false;
int currentServoAngle = 20;
unsigned long lastServoMoveTime = 0;
const unsigned long servoIdleTimeout = 5000;

const int distanceThreshold = 10;
const unsigned long scanTimeout = 25000;

const String authorizedUsers[] = {"Villyan Sutanto", "Marco Linardi", "Javier Ryan", "Johanes Cedrick"};
const int numAuthorizedUsers = sizeof(authorizedUsers) / sizeof(authorizedUsers[0]);

#define WIFI_SSID "Villyan's Samsung S21 FE"
#define WIFI_PASSWORD "Villyan27"
const char* firebaseURL = "https://aol-iot-mr-default-rtdb.firebaseio.com/Report.json";

MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
MFRC522::StatusCode status;

bool scanningRFID = false;
unsigned long scanStartTime = 0;
bool scanMessagePrinted = false;

const int firstNameBlock = 4;
const int lastNameBlock = 5;
byte bufferLen = 32;
byte readBlockData[18];
String card_holder_name;

void displayMessage(String line1, String line2 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, line1.c_str());
  u8g2.drawStr(0, 28, line2.c_str());
  u8g2.sendBuffer();
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "1970-01-01T00:00:00";
  }
  char timeString[25];
  strftime(timeString, sizeof(timeString), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return String(timeString);
}

void triggerCameraCapture(const String& id) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    
    String url = "http://" + String(camIP) + "/capture?id=" + id;
    Serial.print("Triggering camera at: ");
    Serial.println(url);

    if (http.begin(client, url)) {
      http.setTimeout(15000);
      int httpCode = http.GET();
      if (httpCode > 0) {
        Serial.print("Camera response: ");
        Serial.println(httpCode);
        String payload = http.getString();
        Serial.println(payload);
      } else {
        Serial.println("Camera trigger failed: " + http.errorToString(httpCode));
      }
      http.end();
    } else {
      Serial.println("Unable to connect to camera");
    }
  }
}

void sendToFirebase(String name, bool isValid) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;

    String url = String("https://aol-iot-mr-default-rtdb.firebaseio.com/Report.json");
    https.begin(client, url);
    https.addHeader("Content-Type", "application/json");

    String now = getFormattedTime();
    DynamicJsonDocument doc(512);
    doc["createdAt"] = now;
    doc["isValid"] = isValid;
    doc["name"] = name;

    String json;
    serializeJson(doc, json);
    Serial.println("Sending JSON to Firebase:");
    Serial.println(json);

    int httpCode = https.POST(json);
    Serial.print("Firebase response: ");
    Serial.println(httpCode);

    if (httpCode == 200) {
      String response = https.getString();
      Serial.println(response);

      // Extract ID key returned from Firebase
      DynamicJsonDocument resDoc(256);
      DeserializationError err = deserializeJson(resDoc, response);
      if (!err && resDoc.containsKey("name")) {
        String firebaseId = resDoc["name"].as<String>();
        triggerCameraCapture(firebaseId);
      } else {
        Serial.println("❌ Failed to parse Firebase response ID");
      }
    } else {
      Serial.println("❌ Firebase upload failed");
    }

    https.end();
  }
}


String readStringFromBlock(int blockNum) {
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockNum, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Auth failed for block "); Serial.println(blockNum);
    return "";
  }
  status = mfrc522.MIFARE_Read(blockNum, readBlockData, &bufferLen);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Read failed for block "); Serial.println(blockNum);
    return "";
  }
  String result = "";
  for (int i = 0; i < 16; i++) result += (char)readBlockData[i];
  return filterPrintable(result);
}

bool tryReadRFID() {
  for (int attempt = 0; attempt < 3; attempt++) {
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      return true;
    }
    delay(100);
  }
  return false;
}

void refreshServo() {
  if (!servoOpen && servoAttached) {
    myServo.write(currentServoAngle);
  }
}

void smoothServoMove(int startAngle, int endAngle) {
  attachServoIfNeeded();
  int step = (startAngle < endAngle) ? 1 : -1;
  for (int angle = startAngle; angle != endAngle; angle += step) {
    myServo.write(angle);
    delay(10);
  }
  myServo.write(endAngle);
  currentServoAngle = endAngle;
  lastServoMoveTime = millis();
}

void attachServoIfNeeded() {
  if (!servoAttached) {
    myServo.attach(servoPin);
    servoAttached = true;
  }
}

void detachServoIfIdle() {
  if (servoAttached && (millis() - lastServoMoveTime > servoIdleTimeout) && !servoOpen) {
    myServo.detach();
    servoAttached = false;
  }
}

String filterPrintable(String input) {
  String output = "";
  for (int i = 0; i < input.length(); i++) {
    if (isPrintable(input[i])) {
      output += input[i];
    }
  }
  return output;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(4, 16);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(buzzPin, OUTPUT);
  u8g2.begin();

  attachServoIfNeeded();
  myServo.write(currentServoAngle);
  lastServoMoveTime = millis();

  servoRefresher.attach_ms(20, refreshServo);

  displayMessage("Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    displayMessage("WiFi:", "Retrying...");
    delay(500);
  }

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // UTC+7
  displayMessage("WiFi Connected", WiFi.localIP().toString().c_str());
  delay(2000);
  displayMessage("Hello!");

  SPI.begin();
  mfrc522.PCD_Init();
}

void loop() {
  long duration;
  int distance;

  detachServoIfIdle();

  if (!scanningRFID) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    duration = pulseIn(echoPin, HIGH);
    distance = duration * 0.034 / 2;

    displayMessage("Distance: " + String(distance) + " cm");

    if (distance > 0 && distance <= distanceThreshold) {
      displayMessage("Object detected!", "Scan your card");
      scanningRFID = true;
      scanStartTime = millis();
      scanMessagePrinted = false;
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      mfrc522.PCD_Init();
    }
  }

  if (scanningRFID) {
    if (!scanMessagePrinted) {
      tone(buzzPin, 3000); delay(500); noTone(buzzPin);
      displayMessage("Scan your Card");
      scanMessagePrinted = true;
    }

    if (tryReadRFID()) {
      Serial.println("\n** RFID Card Detected **");
      String firstName = readStringFromBlock(firstNameBlock);
      String lastName = readStringFromBlock(lastNameBlock);
      Serial.print("Read FirstName block: "); Serial.println(firstName);
      Serial.print("Read LastName block: "); Serial.println(lastName);
      firstName.trim();
      lastName.trim();
      card_holder_name = firstName + (lastName.isEmpty() ? "" : " " + lastName);
      card_holder_name.trim();

      card_holder_name = filterPrintable(card_holder_name);
      card_holder_name.trim();

      if (card_holder_name.length() == 0) {
        Serial.println("⚠️ Nama kosong atau tidak valid, gunakan 'UNKNOWN'");
        card_holder_name = "UNKNOWN";
      }
      Serial.print("Full Name After Trim: '"); Serial.print(card_holder_name); Serial.println("'");

      bool isAuthorized = false;
      for (int i = 0; i < numAuthorizedUsers; i++) {
        String authName = authorizedUsers[i];
        authName.trim();
        Serial.print("Comparing to authorized: '");
        Serial.print(authName);
        Serial.print("' -> ");
        if (card_holder_name.equalsIgnoreCase(authName)) {
          Serial.println("✅ MATCH");
          isAuthorized = true;
          break;
        } else {
          Serial.println("❌ NO MATCH");
        }
      }

      if (card_holder_name.length() == 0) {
        Serial.println("Warning: card_holder_name is empty!");
        displayMessage("Access Denied", "No Name Read");
        sendToFirebase("UNKNOWN", false);
      } else if (isAuthorized) {
        Serial.println("Result: ✅ AUTHORIZED");
        displayMessage("Hello, " + card_holder_name, "Access Granted");
        for (int i = 0; i < 2; i++) {
          tone(buzzPin, 3000); delay(75); noTone(buzzPin); delay(50);
          tone(buzzPin, 2500); delay(75); noTone(buzzPin); delay(50);
        }
        smoothServoMove(currentServoAngle, 120);
        servoOpen = true;
        delay(3000);
        smoothServoMove(currentServoAngle, 20);
        servoOpen = false;
        sendToFirebase(card_holder_name, true);
      } else {
        Serial.println("Result: ❌ NOT AUTHORIZED");
        tone(buzzPin, 3000); delay(100);
        tone(buzzPin, 2500); delay(100);
        tone(buzzPin, 2000); delay(100); noTone(buzzPin);
        displayMessage("Access Denied", "Unauthorized!");

        if (card_holder_name.length() == 0) {
          sendToFirebase("UNKNOWN", false);
        } else {
          sendToFirebase(card_holder_name, false);
        }
      }

      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      mfrc522.PCD_Reset();
      mfrc522.PCD_Init();

      delay(3000);
      scanningRFID = false;
      u8g2.clearBuffer(); u8g2.sendBuffer();
    }

    if (millis() - scanStartTime > scanTimeout) {
      tone(buzzPin, 3000); delay(2000); noTone(buzzPin);
      displayMessage("Timeout!", "Try again...");
      delay(2000);
      scanningRFID = false;
      u8g2.clearBuffer(); u8g2.sendBuffer();
    }
  }
}
