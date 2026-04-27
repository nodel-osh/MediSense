# Wireless Bedside Health Monitor
**EE105 Final Project** — Osher Nodel, Jake Simons, Dana Sanei, Kaitlyn Ooi, Yanet Dereje, Alexander Garcia

## What is this?

A wireless bedside health monitor using two Arduino Nano 33 BLE Sense Rev1 boards that focuses on temperature, BPM, and humidity to connect a patient with their caretaker without the need of physical checking. One board sits on the patient side and handles all the sensing, and the other sits with the caregiver and receives everything wirelessly over BLE in real time.

The idea came from wanting something low  cost that could work in a hospital or even at home — for parents, caretakers, or anyone looking after someone who needs monitoring. It's built with all sorts of patients in mind: people that run high temperatures when sick, asthma patients, delirium patients who often experience severe sleep disturbances, and people with chronic respiratory conditions like COPD, circulatory issues, arthritis, diabetes, or MS. Extreme temperatures can trigger severe symptoms for a lot of these patients, which is part of why we track humidity and temperature too.


## Hardware you'll need

   2× Arduino Nano 33 BLE Sense Rev1
   MAX30102 (SpO₂ + heart rate sensor, external breakout)
   VL53L0X (infrared proximity sensor, Pololu breakout board)
   HTS221 (built into the Nano — handles temp & humidity)
   5 push buttons + resistors for the voltage divider network

**Wiring the MAX30102 and VL53L0X** — both connect over I²C:
   VIN → 3.3V, GND → GND, SDA → A4, SCL → A5

For the VL53L0X specifically, it needs to be placed in a fixed spot relative to the patient — within about 1.2m and within its 25° cone of detection. If the sensor itself moves, the readings will be off, so mount it somewhere stable like a bedside stand aimed at the patient's torso.

   

## Libraries to install

In Arduino IDE Library Manager, install:

```
ArduinoBLE
Arduino_HTS221
SparkFun MAX3010x Pulse and Proximity Sensor Library
Pololu VL53L0X
```

   

## How to upload

1. Open `patient/patient.ino`, select Arduino Nano 33 BLE as the board, and upload to the patient  side board
2. Open `caregiver/caregiver.ino` and upload to the caregiver  side board
3. Power both — the caregiver board will scan and connect automatically

   

## How it works

### BLE Connection (Osher Nodel, Dana Sanei)

To connect the two boards wirelessly, one is configured as a BLE Peripheral (patient unit) and the other as a BLE Central (caregiver unit) using the ArduinoBLE library. The Peripheral board creates and advertises characteristics for temperature, BPM, humidity, buttons, and motion. The Central continuously scans for the Peripheral's advertised service UUID and once it finds it, connects and subscribes to all the characteristics via BLE notifications. After that, sensor and button data transmit in real time from patient to caregiver without any physical wiring.

### Humidity & Temperature — HTS221

The HTS221 is built right into the Nano 33 BLE Sense Rev1, so no extra wiring needed — just the `Arduino_HTS221` library. It reads temperature in °C and relative humidity as a percentage. Humidity in the room is important since many patients are highly sensitive to cold or heat. Optimal sleeping humidity is generally around 40–60%, and readings outside that range trigger an alert on the caregiver unit.

```cpp
#include <Arduino_HTS221.h>

float temperature = HTS.readTemperature();
float humidity    = HTS.readHumidity();
```

### Blood Oxygen & Heart Rate — MAX30102 (Yanet Dereje, Kaitlyn Ooi)

The MAX30102 uses photoplethysmography (PPG) — it shines red and infrared LEDs through the skin and measures how much light gets absorbed by the blood. SpO₂ is calculated from the ratio of red  to  infrared absorption (normal range is 95–100%), and BPM comes from the peak frequency of that waveform. Raw data gets smoothed with a rolling average to reduce noise before being sent over BLE. If SpO₂ drops below 95% or BPM goes outside a safe range, the caregiver unit triggers an alert.

```cpp
#include <SparkFun_MAX3010x_Sensor_Library.h>

MAX30105 particleSensor;
particleSensor.begin(Wire, I2C_SPEED_FAST);
particleSensor.setup();

long irValue  = particleSensor.getIR();
long redValue = particleSensor.getRed();
```

### Proximity & Sleep Monitoring — VL53L0X (Jake Simons)

The infrared sensor utilizes the VL53L0X IR sensor to detect the sleep of patients to make sure there are no issues while asleep. It's preferred over ultrasonic sensors because its readings aren't affected by humidity fluctuations.

It outputs 3 parameters: **quiet**, **active**, and **sporadic** movement. Quiet indicates no movement and the patient is soundly asleep. Active indicates light movement or turning, and sporadic indicates violent or sudden movement.

To figure out which class the patient is in at any given time, the algorithm computes the average velocity, the mean of that average velocity, the standard deviation of the velocity, and acceleration/jerk across a window of distance samples. Sporadic movement triggers an immediate alert on the caregiver unit — this is especially relevant for delirium patients who can become agitated or physically active at night.

```cpp
#include <VL53L0X.h>

VL53L0X sensor;
sensor.init();
sensor.setTimeout(500);
sensor.startContinuous();

uint16_t distance = sensor.readRangeContinuousMillimeters();
// Feed distance into velocity and std dev computation
```

### Buttons — Voltage Divider Network (Alexander Garcia)

We implemented 5 buttons, each with a different purpose — labeled (or color coded) as: need water, need restroom, need help, yes, and no. Rather than wiring each button to its own pin, we use a voltage divider network so all 5 can be read from a single analog input. Each button has a unique resistor value that produces a unique voltage at the analog pin. A pull  down resistor ensures the pin reads 0V when nothing is pressed. The actual ADC threshold ranges for each button are determined via the Serial Monitor during setup so there's no overlap between presses.

Once a valid button press is detected, its corresponding state gets written to a BLE characteristic and sent to the caregiver unit right away.

```cpp
int analogVal = analogRead(BUTTON_PIN);

if      (analogVal < THRESH_1) buttonState = 1; // Need Water
else if (analogVal < THRESH_2) buttonState = 2; // Need Restroom
else if (analogVal < THRESH_3) buttonState = 3; // Need Help
else if (analogVal < THRESH_4) buttonState = 4; // Yes
else if (analogVal < THRESH_5) buttonState = 5; // No
else                            buttonState = 0; // No press
```

The buttons are especially useful for patients that can't or shouldn't be straining their voice, or for patients with neurological disorders like autism, schizophrenia, or PTSD who don't speak — it lets them communicate without any issues or strain.

   

## Alert thresholds

All thresholds are defined as constants at the top of `caregiver.ino` and can be adjusted per patient. Defaults:

   Temperature: alert if > 38°C (fever) or < 35°C (hypothermia)
   Humidity: alert if < 30% or > 70%
   SpO₂: alert if < 95%
   BPM: alert if < 50 or > 120
   Motion: alert on sustained sporadic movement

   

## Why we built this

The main goal was less stress on caretakers — being able to constantly monitor a patient without the back and forth of physically checking in every few minutes. The buttons mean patients can communicate their needs without wasting valuable caretaker time, and the help button covers emergencies. For the patients themselves, it removes the need to strain their voice or move when they shouldn't have to. And the motion/sleep monitoring gives caretakers a real picture of how the patient is actually resting overnight.
