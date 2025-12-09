
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <FastLED.h>
#include <MPU9250_WE.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

#define MPU9250_ADDR 0x68
#define I2C_SDA 8
#define I2C_SCL 9
#define LED_PIN 10
#define NUM_LEDS 24

#define SERVICE_UUID "7a14991b-629b-4095-8eba-50161c560cc2"
#define SENSOR_CHARACTERISTIC_UUID "3386d13a-0e1e-4a34-8602-813db329ae45"
#define LED_CHARACTERISTIC_UUID "cd9bdd16-0f24-4071-ba06-60a0a276b49a"
#define DEBUG_CHARACTERISTIC "76cb18d1-2bb1-4eed-bd08-bf62e167bbf0"
#define WARTUNG_SERVICE_UUID "a83aa887-e642-4e44-905e-4a7cebe81ed9"
#define WARTUNG_MODE_UUID "aad422b1-87e3-47c1-88db-013456789001"
#define WARTUNG_SSID_UUID "aad422b1-87e3-47c1-88db-013456789002"
#define WARTUNG_PASS_UUID "aad422b1-87e3-47c1-88db-013456789003"
#define WARTUNG_PASSWORT_UUID "aad422b1-87e3-47c1-88db-013456789004"
#define HIT_DELAY 50

CRGB leds[NUM_LEDS];
MPU9250_WE motionSensor = MPU9250_WE(MPU9250_ADDR);

BLEServer* pServer = nullptr;
BLEService* pSensorService = nullptr;
BLECharacteristic* pSensorCharacteristic = nullptr;
BLECharacteristic* pLedColor = nullptr;
BLECharacteristic* pDebugCharacteristic = nullptr;
BLECharacteristic *pWartungMode, *pWartungSSID, *pWartungPass, *pWartungOTApass;

bool deviceConnected = false;
bool oldDeviceConnected = false;
bool debugMode = false;
bool wartungsmodus = false;
bool otaActive = false;
String wlanSSID = "";
String wlanPasswort = "";
String otaPasswort = "";
unsigned long lastHit = 0;

CRGB colors[] = {
  CRGB::Red, CRGB::Blue, CRGB::LightBlue, CRGB::Green,
  CRGB::Yellow, CRGB::Orange, CRGB::Magenta, CRGB(138, 43, 226)
};
#define COLOR_GREEN 3
#define COLOR_RED 0
#define COLOR_OFF CRGB::Black

void setAllLEDs(CRGB color) {
  fill_solid(leds, NUM_LEDS, color);
  FastLED.show();
}

void blinkLED(CRGB color) {
  for (int i = 0; i < 3; i++) {
    setAllLEDs(color);
    delay(100);
    setAllLEDs(COLOR_OFF);
    delay(100);
  }
}

float getStableAcceleration() {
  for (int i = 0; i < 50; i++) {
    float a = motionSensor.getResultantG(motionSensor.getGValues());
    if (!isnan(a) && a > 0.0 && a < 20.0) return a;
    delay(5);
  }
  return -1.0;
}

class BLEServerCallback: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { deviceConnected = true; }
  void onDisconnect(BLEServer* pServer) { deviceConnected = false; }
};

class LEDColorCallBack : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    if (pChar->getValue().length() == 3) {
      uint8_t* data = pChar->getData();
      CRGB color(data[0], data[1], data[2]);
      setAllLEDs(color);
    }
  }
};

class WartungModeCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    wartungsmodus = (*(pChar->getData()) == 1);
    // debugPrintln(wartungsmodus ? "Wartungsmodus EIN" : "Wartungsmodus AUS");
  }
};

class WartungSSIDCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    wlanSSID = String(pChar->getValue().c_str());
    // debugPrintln("SSID empfangen: " + wlanSSID);
  }
};

class WartungPassCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    wlanPasswort = String(pChar->getValue().c_str());
    // debugPrintln("Passwort empfangen.");
  }
};

class WartungOTAPassCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    otaPasswort = String(pChar->getValue().c_str());
    // debugPrintln("OTA-Passwort empfangen.");
  }
};

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(255);
  FastLED.clear();
  FastLED.show();
  motionSensor.init(); motionSensor.autoOffsets();

  BLEDevice::init("BLINKPOD");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BLEServerCallback());

  pSensorService = pServer->createService(SERVICE_UUID);

  pLedColor = pSensorService->createCharacteristic(
    LED_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_WRITE);
  pLedColor->setCallbacks(new LEDColorCallBack());
  pLedColor->addDescriptor(new BLE2902());

  pSensorCharacteristic = pSensorService->createCharacteristic(
    SENSOR_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY);
  pSensorCharacteristic->addDescriptor(new BLE2902());


  pSensorService->start();

  BLEService* pWartungService = pServer->createService(WARTUNG_SERVICE_UUID);
  pWartungMode = pWartungService->createCharacteristic(WARTUNG_MODE_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ);
  pWartungMode->setCallbacks(new WartungModeCallback());

  pWartungSSID = pWartungService->createCharacteristic(WARTUNG_SSID_UUID, BLECharacteristic::PROPERTY_WRITE);
  pWartungSSID->setCallbacks(new WartungSSIDCallback());

  pWartungPass = pWartungService->createCharacteristic(WARTUNG_PASS_UUID, BLECharacteristic::PROPERTY_WRITE);
  pWartungPass->setCallbacks(new WartungPassCallback());

  pWartungOTApass = pWartungService->createCharacteristic(WARTUNG_PASSWORT_UUID, BLECharacteristic::PROPERTY_WRITE);
  pWartungOTApass->setCallbacks(new WartungOTAPassCallback());

  pWartungService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->addServiceUUID(WARTUNG_SERVICE_UUID);
  pAdvertising->start();
}

void loop() {
  if (deviceConnected) {
    float a = getStableAcceleration();
    if (a >= 0 && fabs(a - 1.0) > 0.3 && ((millis()-lastHit) > HIT_DELAY)) {
      pSensorCharacteristic->setValue((uint8_t[]){1}, 1);
      pSensorCharacteristic->notify();
      lastHit = millis();
      // debugPrintln("Hit sent by BLE.");
    }
  }

  if (!deviceConnected && oldDeviceConnected) {
    // debugPrintln("Device disconnected.");
    delay(500);
    pServer->startAdvertising();
    // debugPrintln("Start advertising");
    blinkLED(colors[COLOR_RED]);
    oldDeviceConnected = deviceConnected;
  }

  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    blinkLED(colors[COLOR_GREEN]);
    setAllLEDs(colors[COLOR_GREEN]);
    // debugPrintln("Device Connected");
  }

  if (wartungsmodus && wlanSSID.length() > 0 && wlanPasswort.length() > 0 && !otaActive) {
    // debugPrintln("Verbinde mit WLAN...");
    WiFi.begin(wlanSSID.c_str(), wlanPasswort.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
      // debugPrint(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      // debugPrintln("WLAN verbunden. Starte OTA...");
      if (otaPasswort.length() > 0) {
        ArduinoOTA.setPassword(otaPasswort.c_str());
      }
      ArduinoOTA.begin();
      otaActive = true;
    } else {
      // debugPrintln("WLAN Verbindung fehlgeschlagen");
      wartungsmodus = false;
    }
  }

  if (otaActive) {
    ArduinoOTA.handle();
  }
}
