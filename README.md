# Core Posture: Smart IoT Posture Corrector & Hydration Assistant

**Core Posture** is a bidirectional, wireless biofeedback system designed to correct poor posture in real-time. It consists of a wearable sensor device (Sender) worn on the upper back and a smart desktop display (Receiver).

Unlike passive monitoring apps, Core Posture provides **immediate haptic feedback** (vibration) when you slouch, helping you retrain your muscle memory. It also features a "Blind Mode" algorithm to ensure accurate readings even while vibrating, and a built-in hydration tracker to keep you healthy.

![Project Cover](https://github.com/YourUsername/Core-Posture-Project/blob/main/images/cover.jpg)
*(Replace the link above with the link to your "Posture Good" photo)*

## 🚀 Features
* **Real-Time Slouch Detection:** Triggers an alert if forward tilt (Pitch) exceeds 15 degrees.
* **Haptic Feedback:** The wearable vibrates to physically remind you to sit up.
* **Blind Mode Algorithm:** Custom state-machine logic pauses sensor readings during vibration to prevent data corruption.
* **Bidirectional Control:** Remotely toggle the vibration motor or calibrate the sensor directly from the desktop display.
* **Hydration Tracker:** Integrated water counter with a 60-minute countdown timer and high-visibility "DRINK WATER!" alert.
* **Privacy First:** Uses **ESP-NOW** (Connectionless Wi-Fi) for secure, local communication without needing a router or internet.

---

## 🛠️ Hardware Required

### **1. Wearable Unit (Sender)**
* **Microcontroller:** ESP32-C3 SuperMini
* **Sensor:** MPU6050 (6-Axis Accelerometer/Gyroscope)
* **Feedback:** Coin Vibration Motor (1027 or similar)
* **Power:** 3.7V LiPo Battery (300mAh - 500mAh) + TP4056 Charging Module
* **Driver:** NPN Transistor (2N2222, BC547, or S8050)
* **Switch:** Slide Switch (for power)

### **2. Display Unit (Receiver)**
* **Device:** Espressif ESP32-S3-BOX-3 (Development Kit)

---

## 🔌 Circuit & Wiring (ESP32-C3 Sender)

| Component | ESP32-C3 Pin | Notes |
| :--- | :--- | :--- |
| **MPU6050 SDA** | GPIO 6 | I2C Data |
| **MPU6050 SCL** | GPIO 7 | I2C Clock |
| **Vibration Motor** | GPIO 3 | **MUST** use a transistor driver (Do not connect directly!) |
| **Status LED** | GPIO 8 | Built-in LED on SuperMini |
| **Calibrate Button** | GPIO 9 | Tactile button (Pull-up) |
| **Battery (+)** | 5V Pin | Connect via TP4056 output |
| **Battery (-)** | GND | Common Ground |

> **⚠️ WARNING:** Always connect the LiPo battery output (3.7V-4.2V) to the **5V pin**, NOT the 3.3V pin. The 5V pin connects to the onboard voltage regulator. Connecting a battery directly to the 3.3V pin will destroy the ESP32.

---

## ⚙️ Configuration & Setup

This project uses **ESP-IDF v5.3.1**. Because it relies on the specific S3-BOX-3 display drivers and LVGL fonts, you must configure the environment before building.

### **1. Adding Dependencies**
The project uses the **IDF Component Manager**. You do not need to download libraries manually.
If you are starting from scratch, run these commands in your project terminal to add the specific Board Support Package:

```bash
idf.py add-dependency "espressif/esp-box-3^3.0.3"
