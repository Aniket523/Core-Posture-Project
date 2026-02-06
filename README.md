# Core Posture: Smart IoT Posture Corrector & Hydration Assistant

**Core Posture** is a bidirectional, wireless biofeedback system designed to correct poor posture in real-time. It consists of a wearable sensor device (Sender) worn on the upper back and a smart desktop display (Receiver).

Unlike passive monitoring apps, Core Posture provides **immediate haptic feedback** (vibration) when you slouch, helping you retrain your muscle memory. It also features a "Blind Mode" algorithm to ensure accurate readings even while vibrating, and a built-in hydration tracker to keep you healthy.

![Project Cover](https://github.com/Aniket523/Core-Posture-Project/blob/main/1000073326.jpg)

📸 Demo
(https://www.youtube.com/shorts/flOXENCrFwQ)

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
idf.py add-dependency "lvgl/lvgl^8.3.0"

2. Activating Advanced Fonts

The UI requires specific Montserrat font sizes. You must enable these in the menuconfig or the compilation will fail.

    Open the configuration menu:
    Bash

    idf.py menuconfig

    Navigate to: Component config → LVGL configuration → Font usage

    Enable (Check) the following options:

        [x] Enable Montserrat 12

        [x] Enable Montserrat 14

        [x] Enable Montserrat 20

    Press Q to Save and Quit.

💻 Installation & Flashing
Step 1: Clone the Repository
Bash

git clone [https://github.com/YourUsername/Core-Posture-Project.git](https://github.com/YourUsername/Core-Posture-Project.git)
cd Core-Posture-Project

Step 2: Flash the Wearable (Sender)

    Navigate to the Sender code folder:
    Bash

cd Sender_Code_C3

Set the target to ESP32-C3:
Bash

idf.py set-target esp32c3

Build and Flash:
Bash

    idf.py build flash monitor

Step 3: Flash the Display (Receiver)

    Navigate to the Receiver code folder:
    Bash

cd ../Receiver_Code_S3

Set the target to ESP32-S3:
Bash

idf.py set-target esp32s3

Build and Flash:
Bash

    idf.py build flash monitor

🧠 How It Works
The "Blind Mode" Algorithm

A common issue with haptic wearables is that the vibration motor shakes the accelerometer, creating "noise" that the system interprets as further movement.

My Solution: I implemented a state machine in the Sender code.

    Detect: When Pitch > 15°, the system flags a "Slouch."

    Pause: It immediately stops reading the sensor.

    Act: It fires the vibration motor for 200ms.

    Wait: It waits 50ms for mechanical vibrations to settle.

    Resume: Only then does it resume reading sensor data. This ensures the UI remains stable and accurate, even during active feedback.

📸 Demo

 ![Project Cover](https://github.com/Aniket523/Core-Posture-Project/blob/main/1000073326.jpg)
📜 License

This project is open-source. Created by Aniket vishwakarma.
