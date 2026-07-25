#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_VL53L0X.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>

// --- Standard I2C Initialization ---
LiquidCrystal_I2C lcd(0x27, 16, 2); 
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

// --- Pin Definitions (Pico W) ---
const int PIN_I2C_SDA   = 4;   
const int PIN_I2C_SCL   = 5;   
const int PIN_BUTTON    = 14;  
const int PIN_RED_LED   = 15;  
const int PIN_GREEN_LED = 16;  

// --- EEPROM Addresses ---
const int EEPROM_ADDR_SAVED_HEIGHT = 0; 
const int EEPROM_ADDR_UNIT         = 4; 

// --- Wi-Fi Access Point Credentials ---
const char* AP_SSID = "Digimeter-AP";
const char* AP_PASS = "digimeter123";
WebServer server(80);
bool isWifiEnabled = false;

// --- Enums & System States ---
enum DistanceUnit { UNIT_MM, UNIT_CM, UNIT_INCHES };
DistanceUnit currentUnit = UNIT_MM;

enum SystemMode { MODE_LIVE, MODE_MENU, MODE_IP_POPUP };
SystemMode currentSystemMode = MODE_LIVE;

int selectedMenuItem = 0;
const int TOTAL_MENU_ITEMS = 4; // 0: Unit, 1: Wi-Fi Toggle, 2: Calibrate, 3: Exit

float thresholdDistanceMm = 0.0;
bool isCalibrated = false;
bool isHoldActive = false;
bool isRecallMode = false;

float savedEepromHeightMm = 0.0; // Value explicitly saved to EEPROM
float autoSettledDisplayMm = 0.0; // Settled 10-burst average on screen
float recalledHeightMm = 0.0;

// --- Auto-Settle Engine Variables ---
bool isAutoSettled = false;
float lastStableReading = 0.0;
unsigned long stabilityStartTime = 0;
const unsigned long AUTO_SETTLE_DELAY = 1000; // 1 second stable hold time

// --- Filter & Data Logging Variables ---
float smoothedDistanceMm = 0.0;
float savedLogBuffer[20]; 
int logCount = 0;

// --- Button Engine Variables ---
int lastButtonState = HIGH;
unsigned long buttonPressStartTime = 0;
bool isLongPressHandled = false;

int clickCount = 0;
unsigned long lastClickTime = 0;
const unsigned long debounceDelay = 40;
const unsigned long multiClickWindow = 350;   
const unsigned long longPressDuration = 1000; 

// --- Raw Laser Reader ---
float readRawDistanceMm() {
  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);

  if (measure.RangeStatus != 4) {
    return (float)measure.RangeMilliMeter;
  }
  return -1.0; 
}

// --- 10-Sample Burst Averaging ---
float getBurstAveragedHeightMm() {
  float sumDistance = 0.0;
  int validSamples = 0;

  for (int i = 0; i < 10; i++) {
    float raw = readRawDistanceMm();
    if (raw > 0) {
      sumDistance += raw;
      validSamples++;
    }
    delay(15); 
  }

  if (validSamples > 0) {
    float avgDistance = sumDistance / validSamples;
    float calculatedHeight = thresholdDistanceMm - avgDistance;
    return (calculatedHeight < 0.0f) ? 0.0f : calculatedHeight;
  }

  return 0.0f;
}

// --- Continuous Reader (EMA Filter) ---
float readLiveDistanceMm() {
  float raw = readRawDistanceMm();
  if (raw > 0) {
    if (smoothedDistanceMm <= 0.0) {
      smoothedDistanceMm = raw; 
    } else {
      smoothedDistanceMm = (0.15f * raw) + (0.85f * smoothedDistanceMm);
    }
    return smoothedDistanceMm;
  }
  return -1.0; 
}

// --- Save Action (EEPROM & Web Log) ---
void saveMeasurementToEEPROM(float heightToSave) {
  savedEepromHeightMm = heightToSave;

  EEPROM.put(EEPROM_ADDR_SAVED_HEIGHT, savedEepromHeightMm);
  EEPROM.commit(); 

  if (logCount < 20) {
    savedLogBuffer[logCount++] = savedEepromHeightMm;
  }
}

// --- Anti-Hanging Web Handlers ---
void handleRootWebPage() {
  server.sendHeader("Connection", "close");
  String html = F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
                  "<title>Digimeter</title>"
                  "<style>body{font-family:Arial;text-align:center;background:#121212;color:#fff;margin:0;padding:20px;}"
                  "table{margin:20px auto;border-collapse:collapse;width:90%;max-width:500px;background:#1e1e1e;}"
                  "th,td{padding:12px;border:1px solid #333;}th{background:#00e676;color:#000;}"
                  "a.btn{display:inline-block;padding:12px 24px;background:#00e676;color:#000;text-decoration:none;font-weight:bold;border-radius:5px;margin-top:15px;}</style></head><body>"
                  "<h1>📐 Digimeter Live Dashboard</h1><p>Connected to <b>Digimeter-AP</b></p><table><tr><th>#</th><th>Saved Measurement (mm)</th></tr>");
  
  for (int i = 0; i < logCount; i++) {
    html += "<tr><td>" + String(i + 1) + "</td><td>" + String(savedLogBuffer[i], 1) + " mm</td></tr>";
  }
  if (logCount == 0) {
    html += F("<tr><td colspan='2'>No measurements saved yet! Press button to save.</td></tr>");
  }
  html += F("</table><a href='/download' class='btn'>📥 Download CSV Report</a></body></html>");

  server.send(200, "text/html", html);
}

void handleCSVDownload() {
  server.sendHeader("Connection", "close");
  String csv = "Index,Measurement_mm\n";
  for (int i = 0; i < logCount; i++) {
    csv += String(i + 1) + "," + String(savedLogBuffer[i], 2) + "\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=digimeter_log.csv");
  server.send(200, "text/csv", csv);
}

void handleFastIgnore() {
  server.sendHeader("Connection", "close");
  server.send(204, "text/plain", ""); 
}

void handleNotFound() {
  server.sendHeader("Connection", "close");
  server.send(404, "text/plain", "Not Found");
}

// --- Wi-Fi Toggle Engine ---
void toggleWifi(bool enable) {
  if (enable) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    
    server.on("/", handleRootWebPage);
    server.on("/download", handleCSVDownload);
    server.on("/favicon.ico", handleFastIgnore);
    server.on("/generate_204", handleFastIgnore);
    server.on("/hotspot-detect.html", handleFastIgnore);
    server.onNotFound(handleNotFound);

    server.begin();
    isWifiEnabled = true;

    currentSystemMode = MODE_IP_POPUP;
    lcd.clear();
  } else {
    server.close();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    isWifiEnabled = false;
  }
}

// --- Floor Calibration ---
void performCalibration() {
  digitalWrite(PIN_GREEN_LED, LOW);
  isCalibrated = false;
  smoothedDistanceMm = 0.0; 
  isAutoSettled = false;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Calibrating... ");
  lcd.setCursor(0, 1);
  lcd.print(" Keep Base Clear");

  float totalDist = 0;
  int validSamples = 0;

  for (int i = 0; i < 20; i++) { 
    float d = readRawDistanceMm();
    if (d > 0) {
      totalDist += d;
      validSamples++;
    }
    delay(25);
  }

  if (validSamples >= 8) {
    thresholdDistanceMm = totalDist / validSamples;
    isCalibrated = true;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("   READY TO USE ");
    lcd.setCursor(0, 1);
    lcd.print("Base: ");
    lcd.print(thresholdDistanceMm, 0); 
    lcd.print(" mm");
    delay(1000);

    digitalWrite(PIN_GREEN_LED, HIGH); 
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" Calib Failed!  ");
    lcd.setCursor(0, 1);
    lcd.print(" Hold btn reset ");
    digitalWrite(PIN_GREEN_LED, LOW);
  }
}

// --- Welcome Sequence ---
void runWelcomeSequence() {
  digitalWrite(PIN_GREEN_LED, LOW);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("    WELCOME     ");
  lcd.setCursor(0, 1);
  lcd.print("   DIGIMETER    ");
  delay(1200);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" Initializing...");
  delay(600);

  performCalibration();
}

// --- Click Handlers ---
void handleSingleClick() {
  if (!isCalibrated) return; 

  if (currentSystemMode == MODE_IP_POPUP) {
    currentSystemMode = MODE_LIVE;
    lcd.clear();
    return;
  }

  if (currentSystemMode == MODE_MENU) {
    selectedMenuItem = (selectedMenuItem + 1) % TOTAL_MENU_ITEMS;
    return;
  }

  if (isRecallMode) {
    isRecallMode = false;
    isHoldActive = false;
    isAutoSettled = false;
    return;
  }

  // Single-Click: Explicit HOLD / SAVE Action
  isHoldActive = !isHoldActive; 
  
  if (isHoldActive) {
    float valueToSave = isAutoSettled ? autoSettledDisplayMm : getBurstAveragedHeightMm();
    saveMeasurementToEEPROM(valueToSave);
  } else {
    isAutoSettled = false; 
  }
}

void handleDoubleClick() {
  if (currentSystemMode == MODE_IP_POPUP) {
    currentSystemMode = MODE_LIVE;
    lcd.clear();
    return;
  }

  if (currentSystemMode == MODE_MENU) {
    if (selectedMenuItem == 0) { 
      if (currentUnit == UNIT_MM) currentUnit = UNIT_CM;
      else if (currentUnit == UNIT_CM) currentUnit = UNIT_INCHES;
      else currentUnit = UNIT_MM;
      EEPROM.put(EEPROM_ADDR_UNIT, (int)currentUnit);
      EEPROM.commit();
    } 
    else if (selectedMenuItem == 1) { 
      toggleWifi(!isWifiEnabled);
    } 
    else if (selectedMenuItem == 2) { 
      currentSystemMode = MODE_LIVE;
      performCalibration();
    } 
    else if (selectedMenuItem == 3) { 
      currentSystemMode = MODE_LIVE;
    }
    return;
  }

  // Memory Recall [MEM]
  EEPROM.get(EEPROM_ADDR_SAVED_HEIGHT, recalledHeightMm);
  if (isnan(recalledHeightMm) || recalledHeightMm < 0 || recalledHeightMm > 5000) {
    recalledHeightMm = 0.0;
  }

  isRecallMode = true;
  isHoldActive = false; 
}

void handleTripleClick() {
  if (currentSystemMode == MODE_MENU || currentSystemMode == MODE_IP_POPUP) return;

  if (currentUnit == UNIT_MM) currentUnit = UNIT_CM;
  else if (currentUnit == UNIT_CM) currentUnit = UNIT_INCHES;
  else currentUnit = UNIT_MM;

  EEPROM.put(EEPROM_ADDR_UNIT, (int)currentUnit);
  EEPROM.commit();
}

void handleLongPress() {
  if (currentSystemMode == MODE_IP_POPUP) {
    currentSystemMode = MODE_LIVE;
    lcd.clear();
    return;
  }

  if (currentSystemMode == MODE_LIVE) {
    currentSystemMode = MODE_MENU;
    selectedMenuItem = 0;
    lcd.clear();
  } else {
    currentSystemMode = MODE_LIVE;
    lcd.clear();
  }
}

void setup() {
  pinMode(PIN_RED_LED, OUTPUT);
  pinMode(PIN_GREEN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  digitalWrite(PIN_RED_LED, HIGH);  
  digitalWrite(PIN_GREEN_LED, LOW); 
  
  Wire.setSDA(PIN_I2C_SDA);
  Wire.setSCL(PIN_I2C_SCL);
  Wire.begin();
  Wire.setTimeout(100); 

  lcd.init(); 
  lcd.backlight();

  EEPROM.begin(256); 

  if (!lox.begin(0x29, false, &Wire)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VL53L0X Error!");
    lcd.setCursor(0, 1);
    lcd.print(" Check Wiring ");
    while (1) delay(10);
  }

  int savedUnit = 0;
  EEPROM.get(EEPROM_ADDR_UNIT, savedUnit);
  if (savedUnit >= 0 && savedUnit <= 2) {
    currentUnit = (DistanceUnit)savedUnit;
  }

  runWelcomeSequence();
}

void loop() {
  int reading = digitalRead(PIN_BUTTON);
  unsigned long now = millis();

  // --- Web Server Handler ---
  static unsigned long lastWifiCheck = 0;
  if (isWifiEnabled && (now - lastWifiCheck > 15)) {
    lastWifiCheck = now;
    server.handleClient();
  }

  // --- Button Sampling ---
  if (reading == LOW && lastButtonState == HIGH) {
    buttonPressStartTime = now;
    isLongPressHandled = false;
  } 
  else if (reading == LOW && lastButtonState == LOW) {
    if (!isLongPressHandled && (now - buttonPressStartTime >= longPressDuration)) {
      handleLongPress();
      isLongPressHandled = true;
      clickCount = 0; 
    }
  } 
  else if (reading == HIGH && lastButtonState == LOW) {
    if (!isLongPressHandled && (now - buttonPressStartTime > debounceDelay)) {
      clickCount++;
      lastClickTime = now;
    }
  }
  lastButtonState = reading;

  // --- Multi-Click Processing ---
  if (clickCount > 0 && (now - lastClickTime > multiClickWindow)) {
    if (clickCount == 1) {
      handleSingleClick();     
    } else if (clickCount == 2) {
      handleDoubleClick();     
    } else if (clickCount >= 3) {
      handleTripleClick();     
    }
    clickCount = 0;
  }

  // --- Auto-Settle Engine (Automatically runs 10-burst average once position is steady for 1s) ---
  if (isCalibrated && !isHoldActive && !isRecallMode && currentSystemMode == MODE_LIVE) {
    float currentDist = readLiveDistanceMm();

    if (currentDist > 0) {
      float currentHeight = thresholdDistanceMm - currentDist;
      if (currentHeight < 0.0f) currentHeight = 0.0f;

      // Only check auto-settle if an object (> 2.0mm) is present
      if (currentHeight > 2.0f) {
        
        // Reset timer if hand/object moves (> 1.2mm change)
        if (abs(currentHeight - lastStableReading) > 1.2f) {
          lastStableReading = currentHeight;
          stabilityStartTime = now;
          isAutoSettled = false;
        } 
        // Once position is steady for 1 full second, run 10-burst average and display it
        else if (!isAutoSettled && (now - stabilityStartTime >= AUTO_SETTLE_DELAY)) {
          autoSettledDisplayMm = getBurstAveragedHeightMm(); // Takes precision average automatically!
          isAutoSettled = true; 
        }

      } else {
        // Object removed from base: reset auto-settle
        isAutoSettled = false;
        lastStableReading = 0.0f;
      }
    }
  }

  // --- LCD Display Loop ---
  static unsigned long lastDisplayUpdate = 0;
  if (isCalibrated && (now - lastDisplayUpdate > 100)) { 
    lastDisplayUpdate = now;

    if (currentSystemMode == MODE_IP_POPUP) {
      lcd.setCursor(0, 0);
      lcd.print("IP: ");
      lcd.print(WiFi.softAPIP().toString());
      lcd.print("  ");
      lcd.setCursor(0, 1);
      lcd.print("Click to exit   ");
    }
    else if (currentSystemMode == MODE_MENU) {
      lcd.setCursor(0, 0);
      lcd.print("--- MENU ---    ");
      lcd.setCursor(0, 1);
      
      switch (selectedMenuItem) {
        case 0:
          lcd.print("> Unit: ");
          if (currentUnit == UNIT_MM) lcd.print("MM    ");
          else if (currentUnit == UNIT_CM) lcd.print("CM    ");
          else lcd.print("IN    ");
          break;
        case 1:
          lcd.print("> WiFi: ");
          lcd.print(isWifiEnabled ? "ON [AP] " : "OFF     ");
          break;
        case 2:
          lcd.print("> Re-Calibrate ");
          break;
        case 3:
          lcd.print("> Exit Menu    ");
          break;
      }
    } 
    else {
      // Live Measurement Display Engine
      float finalDisplayValue = 0.0;

      if (isRecallMode) {
        finalDisplayValue = recalledHeightMm;
      } 
      else if (isHoldActive) {
        finalDisplayValue = savedEepromHeightMm; // Saved value
      } 
      else if (isAutoSettled) {
        finalDisplayValue = autoSettledDisplayMm; // Automatically calculated 10-burst average
      } 
      else {
        float currentDistMm = readLiveDistanceMm();
        if (currentDistMm > 0) {
          float rawHeight = thresholdDistanceMm - currentDistMm;
          if (rawHeight < 0) rawHeight = 0.0;
          finalDisplayValue = rawHeight;
        }
      }

      // Line 1 Status Header (No AUTO tag display)
      lcd.setCursor(0, 0);
      lcd.print("Digimeter ");
      if (isWifiEnabled) {
        lcd.print("[W+] ");
      } else if (isRecallMode) {
        lcd.print("[MEM] ");
      } else if (isHoldActive) {
        lcd.print("[HOLD]");
      } else {
        lcd.print("      "); // Clean view
      }

      // Line 2 Value Output
      lcd.setCursor(0, 1);
      lcd.print("H: ");

      switch (currentUnit) {
        case UNIT_MM:
          lcd.print(finalDisplayValue, 1);
          lcd.print(" mm   ");
          break;

        case UNIT_CM:
          lcd.print(finalDisplayValue / 10.0, 2);
          lcd.print(" cm   ");
          break;

        case UNIT_INCHES:
          lcd.print(finalDisplayValue / 25.4, 3);
          lcd.print(" in   ");
          break;
      }
    }
  }
}