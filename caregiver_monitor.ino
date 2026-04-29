#include <ArduinoBLE.h>

// Must match patient code
const char* DEVICE_NAME = "PatientMonitor";

const char* TEMP_UUID  = "19B10011-E8F2-537E-4F6C-D104768A1214";
const char* HUM_UUID   = "19B10012-E8F2-537E-4F6C-D104768A1214";
const char* STATE_UUID = "19B10013-E8F2-537E-4F6C-D104768A1214";
const char* SPO2_UUID  = "19B10014-E8F2-537E-4F6C-D104768A1214";
const char* HR_UUID    = "19B10015-E8F2-537E-4F6C-D104768A1214";

// (button from patient)
const char* REQ_UUID   = "19B10016-E8F2-537E-4F6C-D104768A1214";

BLEDevice patient;

BLECharacteristic tempChar;
BLECharacteristic humChar;
BLECharacteristic stateChar;
BLECharacteristic spo2Char;
BLECharacteristic hrChar;
BLECharacteristic reqChar;

unsigned long lastRead = 0;
const unsigned long READ_INTERVAL = 200;

// Final request string (NO voltage anymore)
String currentRequest = "NONE";

// Decode EXACT patient values
String decodeRequest(byte val) {
  switch (val) {
    case 1: return "Need Water";
    case 2: return "Need Restroom";
    case 3: return "Need Help";
    case 4: return "YES";
    case 5: return "NO";
    default: return "NONE";
  }
}

String movementState(byte state) {
  if (state == 0) return "QUIET";
  if (state == 1) return "ACTIVE";
  if (state == 2) return "SPORADIC";
  return "UNKNOWN";
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("Caretaker Monitor Starting...");

  if (!BLE.begin()) {
    Serial.println("BLE failed to start.");
    while (1);
  }

  Serial.println("Scanning for PatientMonitor...");
  BLE.scanForName(DEVICE_NAME);
}

void loop() {
  if (!patient || !patient.connected()) {
    patient = BLE.available();

    if (patient) {
      Serial.print("Found device: ");
      Serial.println(patient.localName());

      BLE.stopScan();

      if (patient.connect()) {
        Serial.println("Connected to PatientMonitor.");
      } else {
        Serial.println("Connection failed. Scanning again...");
        BLE.scanForName(DEVICE_NAME);
        return;
      }

      if (!patient.discoverAttributes()) {
        Serial.println("Attribute discovery failed.");
        patient.disconnect();
        BLE.scanForName(DEVICE_NAME);
        return;
      }

      tempChar  = patient.characteristic(TEMP_UUID);
      humChar   = patient.characteristic(HUM_UUID);
      stateChar = patient.characteristic(STATE_UUID);
      spo2Char  = patient.characteristic(SPO2_UUID);
      hrChar    = patient.characteristic(HR_UUID);
      reqChar   = patient.characteristic(REQ_UUID); // ✅ IMPORTANT

      if (!tempChar || !humChar || !stateChar || !spo2Char || !hrChar || !reqChar) {
        Serial.println("Could not find all patient characteristics.");
        patient.disconnect();
        BLE.scanForName(DEVICE_NAME);
        return;
      }

      Serial.println("All characteristics found.");
      Serial.println("--------------------------------");
    }

    return;
  }

  if (millis() - lastRead < READ_INTERVAL) return;
  lastRead = millis();

  float temp = 0;
  float hum = 0;
  byte state = 0;
  int spo2 = -1;
  int hr = -1;

  tempChar.readValue((byte*)&temp, sizeof(temp));
  humChar.readValue((byte*)&hum, sizeof(hum));
  stateChar.readValue(state);
  spo2Char.readValue((byte*)&spo2, sizeof(spo2));
  hrChar.readValue((byte*)&hr, sizeof(hr));

  // READ BUTTON FROM PATIENT (THIS IS THE FIX)
  byte reqValue = 0;
  if (reqChar.readValue(reqValue)) {
    currentRequest = decodeRequest(reqValue);
  }

  Serial.println("===== Patient Data =====");

  Serial.print("Temperature: ");
  Serial.print(temp);
  Serial.print(" °C | ");

  if (temp < 20.0) Serial.print("Too Cold");
  else if (temp >= 25.0) Serial.print("Too Warm");
  else Serial.print("Normal");

  Serial.println();

  Serial.print("Humidity: ");
  Serial.print(hum);
  Serial.print(" % | ");

  if (hum > 60.0) Serial.print("Too Humid");
  else if (hum < 30.0) Serial.print("Too Dry");
  else Serial.print("Normal");

  Serial.println();

  Serial.print("Movement: ");
  Serial.println(movementState(state));

  Serial.print("SpO2: ");
  if (spo2 < 0) Serial.println("No valid reading yet");
  else {
    Serial.print(spo2);
    Serial.println(" %");
  }

  Serial.print("Heart Rate: ");
  if (hr < 0) Serial.println("No valid reading yet");
  else {
    Serial.print(hr);
    Serial.println(" bpm");
  }

  // OUTPUT
  Serial.print("Patient Request: ");
  Serial.println(currentRequest);

  Serial.println("========================");
  Serial.println();
}
