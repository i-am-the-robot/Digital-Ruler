# Digimeter

**Digimeter** is a compact, smart digital height & component measurement device built around the **Raspberry Pi Pico W** and the **Adafruit VL53L0X Time-of-Flight (ToF) laser distance sensor**. Designed for measuring small hardware items like screws, nuts, spacers, and electronic components, Digimeter provides instant real-time feedback with automatic stability locking, local memory persistence, and a built-in Wi-Fi Web Dashboard.

---

## Key Features

* **Dual-Stage Measurement Engine:**
  * **Live Mode:** Continuous Exponential Moving Average (EMA) filtering for smooth, flicker-free real-time positioning.
  * **Auto-Settle Engine:** Detects when an object stays steady for **1 second** and automatically executes a **10-sample burst average** to lock in a highly accurate measurement on screen.
* **Smart Floor Calibration:** Automatic tare/zeroing sequence on startup to calibrate against varying surface levels.
* **Onboard Memory Persistence (EEPROM):** Save measurements on demand to internal EEPROM, surviving power cycles.
* **Multi-Unit Support:** Instant switching between **Millimeters (mm)**, **Centimeters (cm)**, and **Inches (in)**.
* **Embedded Web Dashboard (Wi-Fi AP):** Spins up a direct Access Point (`Digimeter-AP`) to view saved measurement logs in real time from any smartphone or browser and export them as **CSV reports**.
* **Intuitive Single-Button UI:** Gesture-based multi-click input for all system interactions.

---

## Hardware Requirements

| Component | Quantity | Description |
| :--- | :---: | :--- |
| **Raspberry Pi Pico W** | 1 | Microcontroller with onboard Wi-Fi |
| **VL53L0X ToF Laser Sensor** | 1 | I2C Laser Ranging Sensor (Adafruit/Generic) |
| **16x2 I2C LCD Display** | 1 | HD44780 LCD with I2C PCF8574 Adapter (`0x27`) |
| **Push Button** | 1 | Tactile momentary switch (Internal Pull-Up) |
| **Status LEDs** | 2 | Green (Ready/Calibrated) & Red (Power/Status) |
| **Breadboard / Custom PCB** | 1 | Wiring platform |

---

## Pinout & Wiring Diagram

| Pico W Pin | Component Pin | Connection Details |
| :--- | :--- | :--- |
| **GP4** | I2C SDA | Connected to VL53L0X SDA & LCD SDA |
| **GP5** | I2C SCL | Connected to VL53L0X SCL & LCD SCL |
| **GP14** | Push Button | Connected to Button (other terminal to GND) |
| **GP15** | Red LED | Power / Status Indicator (via 220Ω resistor to GND) |
| **GP16** | Green LED | Ready / Calibration Indicator (via 220Ω resistor to GND) |
| **3V3 / 5V** | VCC | Power rail for VL53L0X (3.3V) & LCD (5V) |
| **GND** | GND | Common System Ground |

---

## Controls & Navigation

Digimeter uses a single multi-function button to handle all interactions:

| Gesture | Action |
| :--- | :--- |
| **Object Placed (1s)** | **Auto-Settle:** Automatically runs 10-sample burst and locks value on screen. |
| **Single Click** | **Save / Hold `[HOLD]`:** Explicitly saves the current/settled measurement to EEPROM & Web log. |
| **Double Click** | **Memory Recall `[MEM]`:** Recalls the last saved measurement from EEPROM. |
| **Triple Click** | **Unit Switch:** Cycles units directly (`mm` → `cm` → `in`). |
| **Long Press (1s)** | **System Menu:** Opens/Exits settings (Unit Select, Wi-Fi AP Toggle, Re-calibrate). |

---

## Embedded Web Dashboard

1. Open the System Menu (**Long Press**) and enable Wi-Fi.
2. Connect your phone or laptop to the Wi-Fi AP:
   * **SSID:** `Digimeter-AP`
   * **Password:** `digimeter123`
3. Navigate to **`http://192.168.4.1`** in any web browser.
4. View all logged measurements or click **Download CSV Report** for export.

---

## Setup & Installation

1. Open the Arduino IDE.
2. Ensure you have installed the **Raspberry Pi Pico / RP2040 Board Package** (` Earle F. Philhower`).
3. Install the required libraries via the Arduino Library Manager:
   * `Adafruit_VL53L0X`
   * `LiquidCrystal_I2C`
   * `Wire` & `EEPROM` (Built-in)
4. Select board **Raspberry Pi Pico W**.
5. Compile and flash the sketch.

---

## License

This project is open-source and available under the [MIT License](LICENSE).
