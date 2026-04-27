# MediSense = Wireless Bedside Health Monitor

EE105 Final Project

Osher Nodel, Jake Simons, Dana Sanei, Kaitlyn Ooi, Yanet Dereje, Alexander Garcia

## Overview

A wireless bedside health monitor built with two **Arduino Nano 33 BLE Sense Rev1** boards that enables real-time patient monitoring without physical check-ins. The system connects a **patient unit** to a **caregiver unit** over Bluetooth Low Energy (BLE), transmitting vital signs, environmental data, motion classification, and patient-initiated button requests.

**Target use cases:**
- Hospital staff monitoring patients remotely
- At-home caregivers for elderly, chronically ill, or neurologically impaired patients
- Monitoring patients with delirium, respiratory conditions, COPD, asthma, MS, arthritis, or diabetes
- Enabling non-verbal communication for patients with autism, schizophrenia, PTSD, or voice strain

## System Architecture

```
┌─────────────────────────────────┐        BLE         ┌──────────────────────────────┐
│         PATIENT UNIT            │ ◄────────────────► │       CAREGIVER UNIT         │
│   Arduino Nano 33 BLE Sense     │                    │  Arduino Nano 33 BLE Sense   │
│           (Peripheral)          │                    │         (Central)            │
│                                 │                    │                              │
│  • HTS221  – Temp & Humidity    │                    │  • Receives all BLE notifs   │
│  • MAX30102 – SpO₂ & BPM        │                    │  • Displays readings         │
│  • VL53L0X – Proximity/Motion   │                    │  • Triggers threshold alerts │
│  • 5 Buttons (Voltage Divider)  │                    │                              │
└─────────────────────────────────┘                    └──────────────────────────────┘
```

---

## Hardware

### Components
| Arduino Nano 33 BLE Sense Rev1 (×2) | Main microcontrollers | — |
| HTS221 | Built-in temperature & humidity sensor | I²C (built-in) |
| MAX30102 | External SpO₂ & heart rate sensor | I²C |
| VL53L0X | External time-of-flight proximity sensor | I²C (Pololu breakout board) |
| 5× Push buttons | Patient communication buttons | Analog (voltage divider) |
| Resistors | Voltage divider network for buttons | — |

### Wiring Notes

**MAX30102 (SpO₂ / BPM)**
- VIN → 3.3V
- GND → GND
- SDA → A4
- SCL → A5

**VL53L0X (Proximity)**
- VIN → 3.3V
- GND → GND
- SDA → A4
- SCL → A5
- Position the sensor within **~1.2m** of the patient; it has a **25° cone of detection**. Mount it in a fixed location — any movement of the sensor itself will corrupt motion readings.

**Button Voltage Divider Network**
- All 5 buttons share a single analog input pin
- Each button is assigned a unique resistor value, producing a unique voltage at the analog pin
- A pull-down resistor ensures the pin reads 0V when no button is pressed
- Actual ADC threshold ranges are determined empirically via the Serial Monitor

---

## Software

### Libraries Required

Install the following via Arduino IDE Library Manager:

ArduinoBLE
Arduino_HTS221
SparkFun MAX3010x Pulse and Proximity Sensor Library
Pololu VL53L0X


### Uploading

1. Open `patient/patient.ino` in Arduino IDE
2. Select **Arduino Nano 33 BLE** as the board
3. Upload to the **patient-side** board
4. Open `caregiver/caregiver.ino`
5. Upload to the **caregiver-side** board
6. Power both boards — the caregiver board will scan and auto-connect


## How It Works

### BLE Communication

The **Peripheral (patient)** board advertises a custom BLE service containing the following characteristics:

| `TEMP_CHAR_UUID` | Temperature (°C, float) |
| `HUMIDITY_CHAR_UUID` | Relative humidity (%, float) |
| `SPO2_CHAR_UUID` | Blood oxygen saturation (%) |
| `BPM_CHAR_UUID` | Heart rate (BPM) |
| `MOTION_CHAR_UUID` | Motion class (0–3 integer) |
| `BUTTON_CHAR_UUID` | Button state (0–5 integer) |

The **Central (caregiver)** board continuously scans for the Peripheral by its advertised service UUID. Once connected, it subscribes to all characteristics via BLE notifications and receives updates in real time without polling.

---

### Sensor Details

#### Temperature & Humidity — HTS221 (Built-in)

The HTS221 is the built-in capacitive sensor on the Nano 33 BLE Sense Rev1, accessed via the `Arduino_HTS221` library.

- **Temperature** is read in °C and transmitted to the caregiver board
- **Humidity** measures the patient's surrounding environment as a percentage
- Optimal sleeping humidity is generally **40–60%**; readings outside this range trigger an alert on the caregiver unit
- Critical for patients with COPD, asthma, arthritis, or MS who are highly sensitive to environmental conditions

```cpp
#include <Arduino_HTS221.h>

float temperature = HTS.readTemperature();
float humidity    = HTS.readHumidity();
```

---

#### SpO₂ & Heart Rate — MAX30102

The MAX30102 uses **photoplethysmography (PPG)**: it shines red and infrared LEDs through the skin and measures the variation in light absorption caused by pulsing blood flow.

- **SpO₂** (blood oxygen saturation) is derived from the ratio of red-to-infrared absorption. Normal range: **95–100%**
- **BPM** (heart rate) is derived from the peak frequency of the PPG waveform
- Raw sensor data is smoothed using a rolling average to reduce noise before being written to BLE characteristics
- Alerts are triggered on the caregiver unit if SpO₂ drops below 95% or BPM falls outside a configurable safe range

```cpp
#include <SparkFun_MAX3010x_Sensor_Library.h>

MAX30105 particleSensor;
particleSensor.begin(Wire, I2C_SPEED_FAST);
particleSensor.setup();

long irValue  = particleSensor.getIR();
long redValue = particleSensor.getRed();
// Pass to SpO2 and BPM calculation functions
```

---

#### Proximity & Motion Classification — VL53L0X

The VL53L0X uses **time-of-flight (ToF)** infrared ranging to measure distance to the nearest object. It is preferred over ultrasonic sensors because its readings are unaffected by humidity fluctuations.

**Motion Statistics (computed per window)**

Each classification window accumulates a series of distance samples. The following statistics are computed:

| Statistic | Purpose |
|---|---|
| Average velocity | Mean rate of change between samples |
| Mean of average velocity | Smoothed trend across window |
| Standard deviation of velocity | Captures variability / restlessness |
| Acceleration / jerk | Refines detection of sudden vs. gradual movement |

**3 Motion Classes**

| Class | Integer Value | Description |
|---|---|---|
| Quiet | `0` | No movement — patient is soundly asleep |
| Active | `1` | Light movement or turning — low velocity, low variance |
| Sporadic | `2` | Violent or sudden movement — high velocity, high variance; potential distress |

The motion class integer is written to the `MOTION_CHAR_UUID` BLE characteristic and transmitted to the caregiver unit. The **Sporadic** class can be configured to trigger an immediate alert on the caregiver unit, which is particularly relevant for delirium patients who may become agitated or physically active at night.

```cpp
#include <VL53L0X.h>

VL53L0X sensor;
sensor.init();
sensor.setTimeout(500);
sensor.startContinuous();

uint16_t distance = sensor.readRangeContinuousMillimeters();
// Feed distance into velocity and std dev computation
```

> **Placement note:** The sensor must remain **completely stationary** during use. Any movement of the sensor itself will be interpreted as patient motion and corrupt readings. Ideal mounting is on a fixed bedside stand aimed at the patient's torso, within 1.2m.

---

#### Buttons — Voltage Divider Network

Five buttons allow the patient to communicate needs without speaking or moving significantly.

| Button | Label | BLE Value |
|---|---|---|
| 1 | 💧 Need Water | `1` |
| 2 | 🚻 Need Restroom | `2` |
| 3 | 🆘 Need Help | `3` |
| 4 | ✅ Yes | `4` |
| 5 | ❌ No | `5` |

Each button is wired with a unique resistor value in a voltage divider network, producing a distinct voltage at a single analog pin. ADC threshold ranges for each button are calibrated empirically via the Serial Monitor during setup to prevent overlap between buttons.

When a valid press is detected, the corresponding integer state is written to the `BUTTON_CHAR_UUID` BLE characteristic and immediately transmitted to the caregiver unit.

```cpp
int analogVal = analogRead(BUTTON_PIN);

if      (analogVal < THRESH_1) buttonState = 1; // Need Water
else if (analogVal < THRESH_2) buttonState = 2; // Need Restroom
else if (analogVal < THRESH_3) buttonState = 3; // Need Help
else if (analogVal < THRESH_4) buttonState = 4; // Yes
else if (analogVal < THRESH_5) buttonState = 5; // No
else                            buttonState = 0; // No press
```

This system is especially valuable for patients who cannot or should not speak — including those recovering from illness, or those with neurological conditions such as autism, schizophrenia, or PTSD.

---

## Alert Thresholds (Configurable)

| Measurement | Alert Condition |
|---|---|
| Temperature | > 38°C (fever) or < 35°C (hypothermia) |
| Humidity | < 30% or > 70% |
| SpO₂ | < 95% |
| BPM | < 50 or > 120 (configurable) |
| Motion Class | Sporadic (Class 2) sustained over N windows |

Thresholds are defined as constants at the top of `caregiver.ino` and can be adjusted for individual patient needs.

---

## Goals & Motivation

- Reduce caregiver stress by enabling continuous remote monitoring without physical check-ins
- Low-cost alternative to commercial patient monitoring systems — accessible for home use
- Supports a wide range of patients: high fevers, asthma, delirium, neurological disorders
- Enables non-verbal communication for patients who cannot speak or should not strain their voice
- Real-time motion classification helps detect nighttime distress, wandering, or agitation

---

## Team Contributions

| Member | Responsibility |
|---|---|
| Osher Nodel, Dana Sanei | BLE communication between both boards |
| Yanet Dereje, Kaitlyn Ooi | MAX30102 SpO₂/BPM + HTS221 humidity/temperature |
| Alexander Garcia | Voltage divider button network |
| Jake Simons | VL53L0X proximity sensor + motion classification algorithm |
