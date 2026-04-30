#include "Adafruit_VL53L0X.h"
#include <math.h>
#include <Arduino_HTS221.h>
#include <ArduinoBLE.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"


Adafruit_VL53L0X lox;
MAX30105 particleSensor;


// -------------------- BLE --------------------
BLEService healthService("19B10010-E8F2-537E-4F6C-D104768A1214");


BLEFloatCharacteristic tempChar("19B10011-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);
BLEFloatCharacteristic humChar ("19B10012-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);
BLEByteCharacteristic  stateChar("19B10013-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);
BLEIntCharacteristic   spo2Char("19B10014-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);
BLEIntCharacteristic   hrChar  ("19B10015-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);
BLEByteCharacteristic  requestChar("19B10016-E8F2-537E-4F6C-D104768A1214", BLERead | BLENotify);


// -------------------- LED PINS --------------------
const int GREEN_LED  = 2;
const int YELLOW_LED = 3;
const int RED_LED    = 4;


// -------------------- BUTTON --------------------
const int BUTTON_PIN = A0;


// 0 = NONE, 1 = WATER, 2 = RESTROOM, 3 = HELP, 4 = YES, 5 = NO
byte currentRequest = 0;


// -------------------- TEMP/HUM --------------------
unsigned long lastTHRead = 0;
const unsigned long TH_INTERVAL = 3000;
bool hts_ok = false;


float currentTemp = 0;
float currentHum  = 0;


// -------------------- BUTTON FILTER --------------------
float buttonFiltered = 0;
bool buttonInitialized = false;
const float BUTTON_ALPHA = 0.2;


unsigned long buttonStableStart = 0;
const unsigned long BUTTON_HOLD_MS = 80;


int lastReading = 0;
int stableReading = 0;
bool buttonLocked = false;


// -------------------- TEMP STATES --------------------
enum RoomStateTemp { NORMAL_T, TOO_WARM, TOO_COLD };
enum RoomStateHum  { NORMAL_H, TOO_HUMID, TOO_DRY };


// -------------------- VL53 SETTINGS --------------------
const unsigned long SAMPLE_PERIOD_MS = 40;
const float VALID_MIN_MM = 20.0;
const float VALID_MAX_MM = 1800.0;


const float EMA_ALPHA = 0.10;
const float DEADBAND_MM = 5.0;


const int WINDOW_SIZE = 30;
const int LABEL_CONFIRM_WINDOWS = 4;
const int QUIET_CONFIRM_WINDOWS = 2;


float filteredMm = 0.0;
float prevFilteredMm = 0.0;
bool haveFiltered = false;


float speedBuffer[WINDOW_SIZE];
int speedIndex = 0;
int speedCount = 0;
bool bufferFull = false;


unsigned long lastSampleMs = 0;
unsigned long lastIRPrint = 0;


const char* stableLabel = "QUIET";
const char* pendingLabel = "QUIET";
int pendingCount = 0;


struct Stats {
  float meanSpeed;
  float maxSpeed;
  float stddevSpeed;
  int spikeCount;
};


// -------------------- SpO2 SETTINGS --------------------
const byte SPO2_BUF_LEN = 100;
const uint32_t IR_THRESHOLD = 20000UL;
const uint32_t IR_SATURATION = 250000UL;
const unsigned long SPO2_SETTLE_MS = 1500;


const int SPO2_LOW = 94;
const int SPO2_CRITICAL = 90;


uint32_t redBuffer[SPO2_BUF_LEN];
uint32_t irBuffer[SPO2_BUF_LEN];


int32_t spo2 = 0;
int32_t heartRate = 0;
int8_t spo2Valid = 0;
int8_t hrValid = 0;


int currentSpo2 = -1;
int currentHR = -1;


byte spo2FillIdx = 0;
bool spo2_ok = false;


// -------------------- BUTTON --------------------
void handleButtons() {
  int raw = analogRead(BUTTON_PIN);


  if (!buttonInitialized) {
    buttonFiltered = raw;
    buttonInitialized = true;
  } else {
    buttonFiltered = BUTTON_ALPHA * raw + (1.0 - BUTTON_ALPHA) * buttonFiltered;
  }


  int reading = 0;


  if (buttonFiltered >= 980 && buttonFiltered <= 1050) reading = 1;
  else if (buttonFiltered >= 740 && buttonFiltered <= 800) reading = 2;
  else if (buttonFiltered >= 620 && buttonFiltered <= 680) reading = 3;
  else if (buttonFiltered >= 340 && buttonFiltered <= 400) reading = 4;
  else if (buttonFiltered >= 220 && buttonFiltered <= 280) reading = 5;
  else reading = 0;


  if (reading != lastReading) {
    lastReading = reading;
    buttonStableStart = millis();
  }


  if ((millis() - buttonStableStart) > BUTTON_HOLD_MS) {
    if (reading != stableReading) {
      stableReading = reading;


      if (stableReading == 0) {
        buttonLocked = false;
        return;
      }


      if (buttonLocked) return;
      buttonLocked = true;


      if (stableReading == 1) {
        Serial.println("Need Water");
        currentRequest = 1;
      }
      else if (stableReading == 2) {
        Serial.println("Need Restroom");
        currentRequest = 2;
      }
      else if (stableReading == 3) {
        Serial.println("Need Help");
        currentRequest = 3;
      }
      else if (stableReading == 4) {
        Serial.println("YES");
        currentRequest = 4;
      }
      else if (stableReading == 5) {
        Serial.println("NO");
        currentRequest = 5;
      }


      requestChar.writeValue(currentRequest);
    }
  }
}


// -------------------- TEMP/HUM --------------------
RoomStateTemp getTemp(float temp) {
if (temp < 20.0) return TOO_COLD;
else if (temp >= 25.0) return TOO_WARM;
else return NORMAL_T;
}


RoomStateHum getHum(float hum) {
if (hum > 60.0) return TOO_HUMID;
else if (hum < 20.0) return TOO_DRY;
else return NORMAL_H;
}


void handleTempHumidity() {
if (!hts_ok) return;


if (millis() - lastTHRead < TH_INTERVAL) return;
lastTHRead = millis();


currentTemp = HTS.readTemperature();
currentHum = HTS.readHumidity();


// NEW: get states
RoomStateTemp tempState = getTemp(currentTemp);
RoomStateHum humState = getHum(currentHum);


Serial.print("Temperature (°C) = ");
Serial.print(currentTemp);
Serial.print(" | ");


if (tempState == TOO_COLD) Serial.print("Too Cold");
else if (tempState == TOO_WARM) Serial.print("Too Warm");
else Serial.print("Normal");


Serial.print(" | Humidity (%) = ");
Serial.print(currentHum);
Serial.print(" | ");


if (humState == TOO_HUMID) Serial.print("Too Humid");
else if (humState == TOO_DRY) Serial.print("Too Dry");
else Serial.print("Normal");


Serial.println();
}


// -------------------- MOVEMENT --------------------
void addSpeed(float speedMmS) {
  speedBuffer[speedIndex] = speedMmS;
  speedIndex = (speedIndex + 1) % WINDOW_SIZE;


  if (!bufferFull) {
    speedCount++;
    if (speedCount >= WINDOW_SIZE) {
      bufferFull = true;
      speedCount = WINDOW_SIZE;
    }
  }
}


int getWindowCount() {
  return bufferFull ? WINDOW_SIZE : speedCount;
}


void copyWindow(float *out, int count) {
  int start = bufferFull ? speedIndex : 0;
  for (int i = 0; i < count; i++) {
    int idx = (start + i) % WINDOW_SIZE;
    out[i] = speedBuffer[idx];
  }
}


Stats computeStats(const float *buf, int n) {
  Stats s = {0, 0, 0, 0};


  float sum = 0.0;
  for (int i = 0; i < n; i++) {
    sum += buf[i];
    if (buf[i] > s.maxSpeed) s.maxSpeed = buf[i];
  }


  s.meanSpeed = sum / n;


  float varSum = 0.0;
  for (int i = 0; i < n; i++) {
    float d = buf[i] - s.meanSpeed;
    varSum += d * d;
  }


  s.stddevSpeed = sqrtf(varSum / n);


  for (int i = 0; i < n; i++) {
    if (buf[i] >= 30.0) s.spikeCount++;
  }


  return s;
}


const char* classifyMovement(const Stats &s) {
  if (s.meanSpeed < 4.0 && s.maxSpeed < 10.0) return "QUIET";


  if (s.maxSpeed >= 30.0 &&
      s.meanSpeed < 9.0 &&
      s.stddevSpeed >= 6.5 &&
      s.spikeCount <= 2) {
    return "SPORADIC";
  }


  return "ACTIVE";
}


void updateStableLabel(const char* newLabel) {
  int required = (strcmp(newLabel, "QUIET") == 0) ? QUIET_CONFIRM_WINDOWS : LABEL_CONFIRM_WINDOWS;


  if (strcmp(newLabel, pendingLabel) == 0) pendingCount++;
  else {
    pendingLabel = newLabel;
    pendingCount = 1;
  }


  if (pendingCount >= required) stableLabel = pendingLabel;
}


// -------------------- BLOOD OXIMETER --------------------
void handleSpO2() {
  if (!spo2_ok) return;


  static bool waitingForFinger = true;
  static bool settling = false;
  static bool collectingReading = false;
  static bool waitingForRemoval = false;
  static bool printedWaitingMessage = false;
  static unsigned long fingerDetectedTime = 0;
  static unsigned long lastIRDebug = 0;


  if (!particleSensor.available()) {
    particleSensor.check();
    return;
  }


  uint32_t currentRed = particleSensor.getRed();
  uint32_t currentIR = particleSensor.getIR();
  particleSensor.nextSample();


  if (waitingForFinger) {
    if (!printedWaitingMessage) {
      Serial.println("Place finger on MAX30102 sensor to take patient reading.");
      printedWaitingMessage = true;
    }


    if (millis() - lastIRDebug > 1000) {
      lastIRDebug = millis();
      Serial.print("MAX30102 IR=");
      Serial.println(currentIR);
    }


    if (currentIR >= IR_THRESHOLD && currentIR < IR_SATURATION) {
      Serial.print("Finger detected. IR=");
      Serial.println(currentIR);
      Serial.println("Hold still while sensor settles...");


      particleSensor.clearFIFO();


      fingerDetectedTime = millis();
      waitingForFinger = false;
      settling = true;
      collectingReading = false;
      waitingForRemoval = false;
      spo2FillIdx = 0;
    }


    if (currentIR >= IR_SATURATION) {
      Serial.println("Signal saturated. Press lighter or lower brightness.");
    }


    return;
  }


  if (settling) {
    if (currentIR < IR_THRESHOLD) {
      Serial.println("Finger removed during settling. Try again.");
      waitingForFinger = true;
      settling = false;
      printedWaitingMessage = false;
      return;
    }


    if (millis() - fingerDetectedTime < SPO2_SETTLE_MS) {
      return;
    }


    Serial.println("Collecting SpO2 samples...");
    spo2FillIdx = 0;
    settling = false;
    collectingReading = true;
    return;
  }


  if (collectingReading) {
    if (currentIR < IR_THRESHOLD) {
      Serial.println("Finger removed too early. Place finger back on sensor.");
      spo2FillIdx = 0;
      waitingForFinger = true;
      collectingReading = false;
      printedWaitingMessage = false;
      return;
    }


    if (currentIR >= IR_SATURATION) {
      Serial.print("IR=");
      Serial.println(currentIR);
      Serial.println("Signal saturated. Press lighter and try again.");


      spo2FillIdx = 0;
      waitingForFinger = true;
      collectingReading = false;
      printedWaitingMessage = false;
      return;
    }


    redBuffer[spo2FillIdx] = currentRed;
    irBuffer[spo2FillIdx] = currentIR;
    spo2FillIdx++;


    if (spo2FillIdx % 20 == 0) {
      Serial.print("Collecting SpO2 samples: ");
      Serial.print(spo2FillIdx);
      Serial.println("/100");
    }


    if (spo2FillIdx < SPO2_BUF_LEN) return;


    maxim_heart_rate_and_oxygen_saturation(
      irBuffer,
      SPO2_BUF_LEN,
      redBuffer,
      &spo2,
      &spo2Valid,
      &heartRate,
      &hrValid
    );


    Serial.println();
    Serial.println("===== Patient Reading =====");


    if (spo2Valid && spo2 > 0 && spo2 <= 100) {
      currentSpo2 = spo2;
      Serial.print("Oxygen level: ");
      Serial.print(currentSpo2);
      Serial.println("%");
    } else {
      currentSpo2 = -1;
      Serial.println("Oxygen level: Could not get valid reading.");
    }


    if (hrValid && heartRate >= 30 && heartRate <= 220) {
      currentHR = heartRate;
      Serial.print("Heart rate: ");
      Serial.print(currentHR);
      Serial.println(" bpm");
    } else {
      currentHR = -1;
      Serial.println("Heart rate: Could not get valid reading.");
    }


    Serial.print("Temperature (°C): ");
    Serial.println(currentTemp);


    Serial.print("Humidity (%): ");
    Serial.println(currentHum);


    Serial.println("===========================");
    Serial.println("Reading complete. Remove finger to take another reading.");
    Serial.println();


    spo2Char.writeValue(currentSpo2);
    hrChar.writeValue(currentHR);


    collectingReading = false;
    waitingForRemoval = true;
    spo2FillIdx = 0;
    return;
  }


  if (waitingForRemoval) {
    if (currentIR < IR_THRESHOLD) {
      Serial.println("Finger removed. Place finger again for another reading.");


      waitingForFinger = true;
      settling = false;
      collectingReading = false;
      waitingForRemoval = false;
      printedWaitingMessage = false;
    }
  }
}


// -------------------- BLE --------------------
void handleBLE() {
  BLE.poll();


  static unsigned long lastBLE = 0;
  if (millis() - lastBLE < 1000) return;
  lastBLE = millis();


  tempChar.writeValue(currentTemp);
  humChar.writeValue(currentHum);


  byte state = 0;
  if (strcmp(stableLabel, "ACTIVE") == 0) state = 1;
  else if (strcmp(stableLabel, "SPORADIC") == 0) state = 2;


  stateChar.writeValue(state);
  spo2Char.writeValue(currentSpo2);
  hrChar.writeValue(currentHR);
  requestChar.writeValue(currentRequest);
}


// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);
  while (!Serial);


  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);


  if (!lox.begin()) Serial.println("VL53 fail");
  else lox.startRangeContinuous();


  if (!HTS.begin()) Serial.println("HTS221 NOT detected");
  else {
    Serial.println("HTS221 ready");
    hts_ok = true;
  }


  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("ERROR: MAX30102 not found. Check wiring.");
  } else {
    Serial.println("MAX30102 ready.");


    particleSensor.setup(0x1F, 4, 2, 100, 411, 4096);
    particleSensor.setPulseAmplitudeRed(0x4F);
    particleSensor.setPulseAmplitudeIR(0x4F);
    particleSensor.setPulseAmplitudeGreen(0);
    particleSensor.clearFIFO();


    spo2_ok = true;
  }


  if (!BLE.begin()) {
    Serial.println("BLE failed");
  } else {
    BLE.setLocalName("PatientMonitor");
    BLE.setAdvertisedService(healthService);


    healthService.addCharacteristic(tempChar);
    healthService.addCharacteristic(humChar);
    healthService.addCharacteristic(stateChar);
    healthService.addCharacteristic(spo2Char);
    healthService.addCharacteristic(hrChar);
    healthService.addCharacteristic(requestChar);


    BLE.addService(healthService);


    tempChar.writeValue(currentTemp);
    humChar.writeValue(currentHum);
    stateChar.writeValue(0);
    spo2Char.writeValue(currentSpo2);
    hrChar.writeValue(currentHR);
    requestChar.writeValue(currentRequest);


    BLE.advertise();
    Serial.println("BLE advertising as PatientMonitor");
  }
}


// -------------------- LOOP --------------------
void loop() {
  unsigned long now = millis();


  handleButtons();
  handleTempHumidity();
  handleSpO2();
  handleBLE();


  if (now - lastSampleMs < SAMPLE_PERIOD_MS) return;
  unsigned long dtMs = now - lastSampleMs;
  lastSampleMs = now;


  if (!lox.isRangeComplete()) return;


  float rawMm = lox.readRange();
  if (rawMm < VALID_MIN_MM || rawMm > VALID_MAX_MM) return;


  if (!haveFiltered) {
    filteredMm = prevFilteredMm = rawMm;
    haveFiltered = true;
    return;
  }


  prevFilteredMm = filteredMm;
  filteredMm = EMA_ALPHA * rawMm + (1.0 - EMA_ALPHA) * filteredMm;


  float deltaMm = fabsf(filteredMm - prevFilteredMm);
  if (deltaMm < DEADBAND_MM) deltaMm = 0;
  else deltaMm -= DEADBAND_MM;


  float speedMmS = deltaMm * (1000.0 / dtMs);


  addSpeed(speedMmS);


  int n = getWindowCount();
  if (n < 8) return;


  float buf[WINDOW_SIZE];
  copyWindow(buf, n);


  Stats s = computeStats(buf, n);
  updateStableLabel(classifyMovement(s));


  digitalWrite(GREEN_LED,  strcmp(stableLabel, "QUIET") == 0);
  digitalWrite(YELLOW_LED, strcmp(stableLabel, "ACTIVE") == 0);
  digitalWrite(RED_LED,    strcmp(stableLabel, "SPORADIC") == 0);


  if (millis() - lastIRPrint > 200) {
    lastIRPrint = millis();
    Serial.print("Movement state: ");
    Serial.println(stableLabel);
  }

