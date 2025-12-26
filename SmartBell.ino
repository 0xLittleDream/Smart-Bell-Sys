/*
 * Smart Bell System for ESP32 - Enhanced Version
 * 
 * Hardware:
 * - ESP32 DevKit
 * - DS3231 RTC Module (I2C: SDA=21, SCL=22)
 * - SSD1306 OLED 128x64 (I2C: SDA=21, SCL=22)
 * - 5V Relay Module (Signal: GPIO26)
 * - Emergency Button (GPIO27)
 * - Display Toggle Switch (GPIO32)
 * - Manual Bell Switch (GPIO33)
 * 
 * Features:
 * - Web interface for managing bell schedules
 * - Up to 15 presets, each with up to 15 bells
 * - Configurable bell duration
 * - Emergency button with 30-second alarm pattern
 * - Display toggle switch
 * - Manual bell trigger
 * - Persistent storage using LittleFS
 * - RTC for accurate timekeeping
 * - OLED display for status
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// Pin definitions
#define RELAY_PIN 26
#define EMERGENCY_BTN 27
#define DISPLAY_TOGGLE_SW 32
#define MANUAL_BELL_SW 33
#define SDA_PIN 21
#define SCL_PIN 22

// Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// System constraints
#define MAX_PRESETS 15
#define MAX_BELLS_PER_PRESET 15
#define DEFAULT_RELAY_DURATION 3000  // 3 seconds default

// Global objects
RTC_DS3231 rtc;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer server(80);

// AP credentials
const char* ap_ssid = "SmartBell_AP";
const char* ap_password = "12345678";

// Data structures
struct Bell {
  int hour;
  int minute;
  bool triggeredToday;
};

struct Preset {
  String name;
  Bell bells[MAX_BELLS_PER_PRESET];
  int bellCount;
};

Preset presets[MAX_PRESETS];
int presetCount = 0;
int activePresetIndex = -1;
int lastDay = -1;
int bellDuration = DEFAULT_RELAY_DURATION; // Configurable bell duration

// Button and switch states
bool emergencyActive = false;
unsigned long emergencyStartTime = 0;
int emergencyPhase = 0;
bool lastEmergencyBtnState = HIGH;
bool lastManualBellState = HIGH;
bool displayToggleState = false;
bool lastDisplayToggleState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Function prototypes
void setupWiFi();
void setupRTC();
void setupDisplay();
void setupRelay();
void setupButtons();
void setupWebServer();
void loadData();
void saveData();
void checkBells();
void triggerBell(int duration = 0);
void handleEmergency();
void handleButtons();
void updateDisplay();
void handleRoot();
void handleGetPresets();
void handleAddPreset();
void handleDeletePreset();
void handleAddBell();
void handleDeleteBell();
void handleSetActive();
void handleGetStatus();
void handleSetDuration();
String getHTML();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Starting Smart Bell System...");
  
  // Initialize I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return;
  }
  Serial.println("LittleFS initialized");
  
  // Setup components
  setupRelay();
  setupButtons();
  setupRTC();
  setupDisplay();
  setupWiFi();
  setupWebServer();
  
  // Load saved data
  loadData();
  
  Serial.println("System ready!");
  updateDisplay();
}

void loop() {
  server.handleClient();
  handleButtons();
  
  if (emergencyActive) {
    handleEmergency();
  } else {
    checkBells();
  }
  
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 1000) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  delay(10);
}

void setupRelay() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  Serial.println("Relay initialized");
}

void setupButtons() {
  pinMode(EMERGENCY_BTN, INPUT_PULLUP);
  pinMode(DISPLAY_TOGGLE_SW, INPUT_PULLUP);
  pinMode(MANUAL_BELL_SW, INPUT_PULLUP);
  Serial.println("Buttons initialized");
}

void setupRTC() {
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    return;
  }
  
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  Serial.println("RTC initialized");
}

void setupDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    return;
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Smart Bell System");
  display.println("Initializing...");
  display.display();
  
  Serial.println("Display initialized");
}

void setupWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi AP Active");
  display.print("IP: ");
  display.println(IP);
  display.display();
  delay(2000);
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/api/presets", HTTP_GET, handleGetPresets);
  server.on("/api/preset/add", HTTP_POST, handleAddPreset);
  server.on("/api/preset/delete", HTTP_POST, handleDeletePreset);
  server.on("/api/bell/add", HTTP_POST, handleAddBell);
  server.on("/api/bell/delete", HTTP_POST, handleDeleteBell);
  server.on("/api/setactive", HTTP_POST, handleSetActive);
  server.on("/api/status", HTTP_GET, handleGetStatus);
  server.on("/api/setduration", HTTP_POST, handleSetDuration);
  
  server.begin();
  Serial.println("Web server started");
}

void loadData() {
  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    Serial.println("No config file found, starting fresh");
    return;
  }
  
  StaticJsonDocument<8192> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.println("Failed to parse config file");
    return;
  }
  
  presetCount = doc["presetCount"] | 0;
  activePresetIndex = doc["activePreset"] | -1;
  bellDuration = doc["bellDuration"] | DEFAULT_RELAY_DURATION;
  
  JsonArray presetsArray = doc["presets"];
  for (int i = 0; i < presetCount && i < MAX_PRESETS; i++) {
    JsonObject presetObj = presetsArray[i];
    presets[i].name = presetObj["name"].as<String>();
    presets[i].bellCount = presetObj["bellCount"] | 0;
    
    JsonArray bellsArray = presetObj["bells"];
    for (int j = 0; j < presets[i].bellCount && j < MAX_BELLS_PER_PRESET; j++) {
      JsonObject bellObj = bellsArray[j];
      presets[i].bells[j].hour = bellObj["hour"];
      presets[i].bells[j].minute = bellObj["minute"];
      presets[i].bells[j].triggeredToday = bellObj["triggered"] | false;
    }
  }
  
  Serial.println("Data loaded successfully");
}

void saveData() {
  StaticJsonDocument<8192> doc;
  
  doc["presetCount"] = presetCount;
  doc["activePreset"] = activePresetIndex;
  doc["bellDuration"] = bellDuration;
  
  JsonArray presetsArray = doc.createNestedArray("presets");
  for (int i = 0; i < presetCount; i++) {
    JsonObject presetObj = presetsArray.createNestedObject();
    presetObj["name"] = presets[i].name;
    presetObj["bellCount"] = presets[i].bellCount;
    
    JsonArray bellsArray = presetObj.createNestedArray("bells");
    for (int j = 0; j < presets[i].bellCount; j++) {
      JsonObject bellObj = bellsArray.createNestedObject();
      bellObj["hour"] = presets[i].bells[j].hour;
      bellObj["minute"] = presets[i].bells[j].minute;
      bellObj["triggered"] = presets[i].bells[j].triggeredToday;
    }
  }
  
  File file = LittleFS.open("/config.json", "w");
  if (!file) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  
  serializeJson(doc, file);
  file.close();
  
  Serial.println("Data saved successfully");
}

void handleButtons() {
  // Emergency button
  bool emergencyBtnState = digitalRead(EMERGENCY_BTN);
  if (emergencyBtnState == LOW && lastEmergencyBtnState == HIGH && !emergencyActive) {
    delay(50); // Debounce
    if (digitalRead(EMERGENCY_BTN) == LOW) {
      emergencyActive = true;
      emergencyStartTime = millis();
      emergencyPhase = 0;
      Serial.println("EMERGENCY ACTIVATED!");
    }
  }
  lastEmergencyBtnState = emergencyBtnState;
  
  // Display toggle switch
  bool displayToggleReading = digitalRead(DISPLAY_TOGGLE_SW);
  if (displayToggleReading != lastDisplayToggleState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (displayToggleReading != displayToggleState) {
      displayToggleState = displayToggleReading;
    }
  }
  lastDisplayToggleState = displayToggleReading;
  
  // Manual bell switch
  bool manualBellState = digitalRead(MANUAL_BELL_SW);
  if (manualBellState == LOW && lastManualBellState == HIGH) {
    delay(50); // Debounce
    if (digitalRead(MANUAL_BELL_SW) == LOW) {
      Serial.println("Manual bell triggered!");
      triggerBell();
    }
  }
  lastManualBellState = manualBellState;
}

void handleEmergency() {
  unsigned long elapsed = millis() - emergencyStartTime;
  
  // Pattern: 5sec ON, 1sec OFF, repeated 5 times (total 30 seconds)
  // Phases: 0=ON1, 1=OFF1, 2=ON2, 3=OFF2, 4=ON3, 5=OFF3, 6=ON4, 7=OFF4, 8=ON5, 9=END
  
  if (emergencyPhase < 9) {
    if (emergencyPhase % 2 == 0) { // ON phase
      digitalWrite(RELAY_PIN, HIGH);
      if (elapsed >= 5000) {
        emergencyStartTime = millis();
        emergencyPhase++;
      }
    } else { // OFF phase
      digitalWrite(RELAY_PIN, LOW);
      if (elapsed >= 1000) {
        emergencyStartTime = millis();
        emergencyPhase++;
      }
    }
  } else {
    // Emergency complete
    digitalWrite(RELAY_PIN, LOW);
    emergencyActive = false;
    Serial.println("Emergency sequence completed");
  }
}

void checkBells() {
  if (!rtc.begin()) return;
  
  DateTime now = rtc.now();
  
  // Check if it's a new day - reset all triggered flags
  if (lastDay != now.day()) {
    lastDay = now.day();
    for (int i = 0; i < presetCount; i++) {
      for (int j = 0; j < presets[i].bellCount; j++) {
        presets[i].bells[j].triggeredToday = false;
      }
    }
    saveData();
    Serial.println("New day - reset all bell triggers");
  }
  
  // Check active preset bells
  if (activePresetIndex >= 0 && activePresetIndex < presetCount) {
    Preset* activePreset = &presets[activePresetIndex];
    
    for (int i = 0; i < activePreset->bellCount; i++) {
      Bell* bell = &activePreset->bells[i];
      
      if (!bell->triggeredToday && 
          bell->hour == now.hour() && 
          bell->minute == now.minute()) {
        triggerBell();
        bell->triggeredToday = true;
        saveData();
        Serial.printf("Bell triggered: %02d:%02d\n", bell->hour, bell->minute);
      }
    }
  }
}

void triggerBell(int duration) {
  if (duration == 0) duration = bellDuration;
  Serial.printf("RINGING BELL for %d ms!\n", duration);
  digitalWrite(RELAY_PIN, HIGH);
  delay(duration);
  digitalWrite(RELAY_PIN, LOW);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  // Emergency display
  if (emergencyActive) {
    display.setTextSize(3);
    display.setCursor(0, 10);
    display.println("EMERG");
    display.println("ENCY!");
    display.display();
    return;
  }
  
  // Normal display based on toggle switch
  if (displayToggleState == LOW) {
    // Show time and date
    if (rtc.begin()) {
      DateTime now = rtc.now();
      display.setTextSize(2);
      display.printf("%02d:%02d:%02d\n", now.hour(), now.minute(), now.second());
      display.setTextSize(1);
      display.printf("%02d/%02d/%04d\n", now.day(), now.month(), now.year());
    } else {
      display.println("RTC Error");
    }
    
    display.println("----------------");
    
    // Display active preset
    if (activePresetIndex >= 0 && activePresetIndex < presetCount) {
      display.print("Preset: ");
      display.println(presets[activePresetIndex].name);
      
      // Find next bell
      DateTime now = rtc.now();
      int currentMinutes = now.hour() * 60 + now.minute();
      int nextBellMinutes = 24 * 60; // End of day
      bool found = false;
      
      for (int i = 0; i < presets[activePresetIndex].bellCount; i++) {
        Bell* bell = &presets[activePresetIndex].bells[i];
        int bellMinutes = bell->hour * 60 + bell->minute;
        
        if (!bell->triggeredToday && bellMinutes > currentMinutes && bellMinutes < nextBellMinutes) {
          nextBellMinutes = bellMinutes;
          found = true;
        }
      }
      
      if (found) {
        display.print("Next: ");
        display.printf("%02d:%02d\n", nextBellMinutes / 60, nextBellMinutes % 60);
      } else {
        display.println("No more bells");
      }
      
      display.printf("Bells: %d\n", presets[activePresetIndex].bellCount);
    } else {
      display.println("No active preset");
    }
  } else {
    // Alternative display: Show preset name and next bell only
    display.setTextSize(1);
    if (activePresetIndex >= 0 && activePresetIndex < presetCount) {
      // Preset name
      display.println("Active Preset:");
      display.setTextSize(2);
      display.println(presets[activePresetIndex].name);
      display.setTextSize(1);
      display.println("");
      
      // Find next bell
      if (rtc.begin()) {
        DateTime now = rtc.now();
        int currentMinutes = now.hour() * 60 + now.minute();
        int nextBellMinutes = 24 * 60;
        bool found = false;
        
        for (int i = 0; i < presets[activePresetIndex].bellCount; i++) {
          Bell* bell = &presets[activePresetIndex].bells[i];
          int bellMinutes = bell->hour * 60 + bell->minute;
          
          if (!bell->triggeredToday && bellMinutes > currentMinutes && bellMinutes < nextBellMinutes) {
            nextBellMinutes = bellMinutes;
            found = true;
          }
        }
        
        if (found) {
          display.println("Next Bell:");
          display.setTextSize(3);
          display.printf("%02d:%02d", nextBellMinutes / 60, nextBellMinutes % 60);
        } else {
          display.setTextSize(2);
          display.println("No more");
          display.println("bells");
        }
      }
    } else {
      display.setTextSize(2);
      display.println("No active");
      display.println("preset");
    }
  }
  
  display.display();
}

void handleRoot() {
  server.send(200, "text/html", getHTML());
}

void handleGetPresets() {
  StaticJsonDocument<8192> doc;
  
  JsonArray presetsArray = doc.createNestedArray("presets");
  for (int i = 0; i < presetCount; i++) {
    JsonObject presetObj = presetsArray.createNestedObject();
    presetObj["id"] = i;
    presetObj["name"] = presets[i].name;
    presetObj["bellCount"] = presets[i].bellCount;
    presetObj["active"] = (i == activePresetIndex);
    
    JsonArray bellsArray = presetObj.createNestedArray("bells");
    for (int j = 0; j < presets[i].bellCount; j++) {
      JsonObject bellObj = bellsArray.createNestedObject();
      bellObj["id"] = j;
      bellObj["hour"] = presets[i].bells[j].hour;
      bellObj["minute"] = presets[i].bells[j].minute;
      bellObj["triggered"] = presets[i].bells[j].triggeredToday;
    }
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleAddPreset() {
  if (presetCount >= MAX_PRESETS) {
    server.send(400, "text/plain", "Maximum presets reached");
    return;
  }
  
  String name = server.arg("name");
  if (name.length() == 0) {
    server.send(400, "text/plain", "Name required");
    return;
  }
  
  presets[presetCount].name = name;
  presets[presetCount].bellCount = 0;
  presetCount++;
  
  saveData();
  server.send(200, "text/plain", "Preset added");
}

void handleDeletePreset() {
  int id = server.arg("id").toInt();
  
  if (id < 0 || id >= presetCount) {
    server.send(400, "text/plain", "Invalid preset ID");
    return;
  }
  
  // Shift presets down
  for (int i = id; i < presetCount - 1; i++) {
    presets[i] = presets[i + 1];
  }
  presetCount--;
  
  // Update active preset index
  if (activePresetIndex == id) {
    activePresetIndex = -1;
  } else if (activePresetIndex > id) {
    activePresetIndex--;
  }
  
  saveData();
  server.send(200, "text/plain", "Preset deleted");
}

void handleAddBell() {
  int presetId = server.arg("preset").toInt();
  int hour = server.arg("hour").toInt();
  int minute = server.arg("minute").toInt();
  
  if (presetId < 0 || presetId >= presetCount) {
    server.send(400, "text/plain", "Invalid preset ID");
    return;
  }
  
  if (presets[presetId].bellCount >= MAX_BELLS_PER_PRESET) {
    server.send(400, "text/plain", "Maximum bells reached for this preset");
    return;
  }
  
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    server.send(400, "text/plain", "Invalid time");
    return;
  }
  
  Bell* bell = &presets[presetId].bells[presets[presetId].bellCount];
  bell->hour = hour;
  bell->minute = minute;
  bell->triggeredToday = false;
  presets[presetId].bellCount++;
  
  saveData();
  server.send(200, "text/plain", "Bell added");
}

void handleDeleteBell() {
  int presetId = server.arg("preset").toInt();
  int bellId = server.arg("bell").toInt();
  
  if (presetId < 0 || presetId >= presetCount) {
    server.send(400, "text/plain", "Invalid preset ID");
    return;
  }
  
  if (bellId < 0 || bellId >= presets[presetId].bellCount) {
    server.send(400, "text/plain", "Invalid bell ID");
    return;
  }
  
  // Shift bells down
  for (int i = bellId; i < presets[presetId].bellCount - 1; i++) {
    presets[presetId].bells[i] = presets[presetId].bells[i + 1];
  }
  presets[presetId].bellCount--;
  
  saveData();
  server.send(200, "text/plain", "Bell deleted");
}

void handleSetActive() {
  int id = server.arg("id").toInt();
  
  if (id < -1 || id >= presetCount) {
    server.send(400, "text/plain", "Invalid preset ID");
    return;
  }
  
  activePresetIndex = id;
  saveData();
  server.send(200, "text/plain", "Active preset set");
}

void handleSetDuration() {
  int duration = server.arg("duration").toInt();
  
  if (duration < 100 || duration > 30000) {
    server.send(400, "text/plain", "Duration must be between 100ms and 30000ms");
    return;
  }
  
  bellDuration = duration;
  saveData();
  server.send(200, "text/plain", "Bell duration updated");
}

void handleGetStatus() {
  StaticJsonDocument<512> doc;
  
  if (rtc.begin()) {
    DateTime now = rtc.now();
    doc["hour"] = now.hour();
    doc["minute"] = now.minute();
    doc["second"] = now.second();
    doc["day"] = now.day();
    doc["month"] = now.month();
    doc["year"] = now.year();
  }
  
  doc["activePreset"] = activePresetIndex;
  if (activePresetIndex >= 0) {
    doc["activePresetName"] = presets[activePresetIndex].name;
  }
  doc["bellDuration"] = bellDuration;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

String getHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Smart Bell System</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: Arial, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
    }
    .container {
      max-width: 900px;
      margin: 0 auto;
      background: white;
      border-radius: 15px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.3);
      padding: 30px;
    }
    h1 {
      color: #667eea;
      margin-bottom: 10px;
      text-align: center;
    }
    .status {
      background: #f0f4ff;
      padding: 15px;
      border-radius: 10px;
      margin-bottom: 20px;
      text-align: center;
    }
    .time {
      font-size: 2em;
      font-weight: bold;
      color: #667eea;
    }
    .section {
      margin-bottom: 30px;
    }
    .section h2 {
      color: #764ba2;
      margin-bottom: 15px;
      border-bottom: 2px solid #667eea;
      padding-bottom: 5px;
    }
    .input-group {
      display: flex;
      gap: 10px;
      margin-bottom: 15px;
      flex-wrap: wrap;
    }
    input, select {
      padding: 10px;
      border: 2px solid #ddd;
      border-radius: 5px;
      font-size: 14px;
      flex: 1;
      min-width: 150px;
    }
    button {
      padding: 10px 20px;
      background: #667eea;
      color: white;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      font-size: 14px;
      font-weight: bold;
      transition: background 0.3s;
    }
    button:hover {
      background: #764ba2;
    }
    button.danger {
      background: #e74c3c;
    }
    button.danger:hover {
      background: #c0392b;
    }
    button.success {
      background: #27ae60;
    }
    button.success:hover {
      background: #229954;
    }
    .preset-card {
      background: #f8f9fa;
      padding: 15px;
      border-radius: 10px;
      margin-bottom: 15px;
      border: 2px solid #ddd;
    }
    .preset-card.active {
      border-color: #27ae60;
      background: #d5f4e6;
    }
    .preset-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 10px;
    }
    .preset-name {
      font-size: 1.2em;
      font-weight: bold;
      color: #2c3e50;
    }
    .preset-actions {
      display: flex;
      gap: 5px;
    }
    .bell-list {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(100px, 1fr));
      gap: 10px;
      margin-top: 10px;
    }
    .bell-item {
      background: white;
      padding: 8px;
      border-radius: 5px;
      text-align: center;
      border: 1px solid #ddd;
      position: relative;
    }
    .bell-item.triggered {
      background: #f0f0f0;
      opacity: 0.6;
    }
    .bell-time {
      font-weight: bold;
      color: #667eea;
    }
    .bell-delete {
      position: absolute;
      top: 2px;
      right: 2px;
      background: #e74c3c;
      color: white;
      border: none;
      border-radius: 3px;
      width: 20px;
      height: 20px;
      cursor: pointer;
      font-size: 12px;
      line-height: 1;
      padding: 0;
    }
    .badge {
      display: inline-block;
      padding: 3px 8px;
      background: #667eea;
      color: white;
      border-radius: 12px;
      font-size: 0.8em;
      margin-left: 5px;
    }
    .badge.active {
      background: #27ae60;
    }
    .duration-setting {
      background: #fff3cd;
      padding: 15px;
      border-radius: 10px;
      margin-bottom: 20px;
      border: 2px solid #ffc107;
    }
    .duration-display {
      font-weight: bold;
      color: #856404;
      margin-left: 10px;
    }
    @media (max-width: 600px) {
      .input-group {
        flex-direction: column;
      }
      input, button {
        width: 100%;
      }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>🔔 Smart Bell System</h1>
    
    <div class="status">
      <div class="time" id="currentTime">--:--:--</div>
      <div id="activePresetName">No active preset</div>
    </div>

    <div class="section">
      <h2>Add New Preset</h2>
      <div class="input-group">
        <input type="text" id="presetName" placeholder="Preset name (e.g., Weekday)" maxlength="30">
        <button onclick="addPreset()">Add Preset</button>
      </div>
    </div>

    <div class="section">
      <h2>Presets</h2>
      <div id="presetsList">Loading...</div>
    </div>

    <div class="duration-setting">
      <h3 style="margin-bottom: 10px; color: #856404;">⏱️ Bell Duration Setting</h3>
      <div class="input-group">
        <input type="number" id="bellDuration" min="100" max="30000" step="100" placeholder="Duration (ms)" style="max-width:200px">
        <button onclick="setDuration()">Set Duration</button>
        <span class="duration-display">Current: <span id="currentDuration">3000</span> ms</span>
      </div>
      <p style="color: #856404; font-size: 0.9em; margin-top: 10px;">
        Set how long the bell rings (100-30000 milliseconds). Example: 3000 = 3 seconds
      </p>
    </div>
  </div>

  <script>
    let presets = [];
    let selectedPreset = null;
    let userIsTyping = false;
    let typingTimeout = null;

    // Detect when user is typing
    document.addEventListener('DOMContentLoaded', function() {
      const inputs = document.querySelectorAll('input');
      inputs.forEach(input => {
        input.addEventListener('focus', function() {
          userIsTyping = true;
        });
        input.addEventListener('blur', function() {
          userIsTyping = false;
        });
        input.addEventListener('input', function() {
          userIsTyping = true;
          clearTimeout(typingTimeout);
          typingTimeout = setTimeout(() => {
            userIsTyping = false;
          }, 3000);
        });
      });
    });

    function updateStatus() {
      fetch('/api/status')
        .then(r => r.json())
        .then(data => {
          const time = `${pad(data.hour)}:${pad(data.minute)}:${pad(data.second)}`;
          document.getElementById('currentTime').textContent = time;
          
          if (data.activePreset >= 0) {
            document.getElementById('activePresetName').textContent = 
              `Active: ${data.activePresetName}`;
          } else {
            document.getElementById('activePresetName').textContent = 
              'No active preset';
          }
          
          document.getElementById('currentDuration').textContent = data.bellDuration;
        });
    }

    function loadPresets() {
      // Don't reload if user is typing
      if (userIsTyping) {
        return;
      }
      
      fetch('/api/presets')
        .then(r => r.json())
        .then(data => {
          presets = data.presets;
          renderPresets();
        });
    }

    function renderPresets() {
      const container = document.getElementById('presetsList');
      if (presets.length === 0) {
        container.innerHTML = '<p style="color: #999;">No presets yet. Add one above!</p>';
        return;
      }

      container.innerHTML = presets.map(preset => `
        <div class="preset-card ${preset.active ? 'active' : ''}">
          <div class="preset-header">
            <div>
              <span class="preset-name">${preset.name}</span>
              <span class="badge">${preset.bellCount} bells</span>
              ${preset.active ? '<span class="badge active">ACTIVE</span>' : ''}
            </div>
            <div class="preset-actions">
              ${!preset.active ? 
                `<button class="success" onclick="setActive(${preset.id})">Activate</button>` :
                `<button onclick="setActive(-1)">Deactivate</button>`
              }
              <button class="danger" onclick="deletePreset(${preset.id})">Delete</button>
            </div>
          </div>
          
          <div class="input-group">
            <input type="number" id="hour_${preset.id}" min="0" max="23" placeholder="Hour" style="max-width:80px" 
              onfocus="userIsTyping=true" onblur="userIsTyping=false">
            <input type="number" id="minute_${preset.id}" min="0" max="59" placeholder="Min" style="max-width:80px"
              onfocus="userIsTyping=true" onblur="userIsTyping=false">
            <button onclick="addBell(${preset.id})">Add Bell</button>
          </div>
          
          <div class="bell-list">
            ${preset.bells.map(bell => `
              <div class="bell-item ${bell.triggered ? 'triggered' : ''}">
                <button class="bell-delete" onclick="deleteBell(${preset.id}, ${bell.id})">×</button>
                <div class="bell-time">${pad(bell.hour)}:${pad(bell.minute)}</div>
                ${bell.triggered ? '<div style="font-size:0.7em;color:#999;">Done</div>' : ''}
              </div>
            `).join('')}
          </div>
        </div>
      `).join('');
    }

    function addPreset() {
      const name = document.getElementById('presetName').value.trim();
      if (!name) {
        alert('Please enter a preset name');
        return;
      }

      const formData = new URLSearchParams();
      formData.append('name', name);

      fetch('/api/preset/add', {
        method: 'POST',
        body: formData
      })
      .then(r => r.text())
      .then(() => {
        document.getElementById('presetName').value = '';
        loadPresets();
      })
      .catch(err => alert('Error: ' + err));
    }

    function deletePreset(id) {
      if (!confirm('Delete this preset and all its bells?')) return;

      const formData = new URLSearchParams();
      formData.append('id', id);

      fetch('/api/preset/delete', {
        method: 'POST',
        body: formData
      })
      .then(() => loadPresets())
      .catch(err => alert('Error: ' + err));
    }

    function addBell(presetId) {
      const hour = document.getElementById(`hour_${presetId}`).value;
      const minute = document.getElementById(`minute_${presetId}`).value;

      if (hour === '' || minute === '') {
        alert('Please enter both hour and minute');
        return;
      }

      const formData = new URLSearchParams();
      formData.append('preset', presetId);
      formData.append('hour', hour);
      formData.append('minute', minute);

      fetch('/api/bell/add', {
        method: 'POST',
        body: formData
      })
      .then(r => r.text())
      .then(() => {
        document.getElementById(`hour_${presetId}`).value = '';
        document.getElementById(`minute_${presetId}`).value = '';
        loadPresets();
      })
      .catch(err => alert('Error: ' + err));
    }

    function deleteBell(presetId, bellId) {
      const formData = new URLSearchParams();
      formData.append('preset', presetId);
      formData.append('bell', bellId);

      fetch('/api/bell/delete', {
        method: 'POST',
        body: formData
      })
      .then(() => loadPresets())
      .catch(err => alert('Error: ' + err));
    }

    function setActive(id) {
      const formData = new URLSearchParams();
      formData.append('id', id);

      fetch('/api/setactive', {
        method: 'POST',
        body: formData
      })
      .then(() => {
        loadPresets();
        updateStatus();
      })
      .catch(err => alert('Error: ' + err));
    }

    function setDuration() {
      const duration = document.getElementById('bellDuration').value;
      if (!duration || duration < 100 || duration > 30000) {
        alert('Please enter a duration between 100 and 30000 milliseconds');
        return;
      }

      const formData = new URLSearchParams();
      formData.append('duration', duration);

      fetch('/api/setduration', {
        method: 'POST',
        body: formData
      })
      .then(r => r.text())
      .then(() => {
        document.getElementById('bellDuration').value = '';
        updateStatus();
        alert('Bell duration updated!');
      })
      .catch(err => alert('Error: ' + err));
    }

    function pad(n) {
      return n.toString().padStart(2, '0');
    }

    // Initialize
    loadPresets();
    updateStatus();
    setInterval(updateStatus, 1000);
    setInterval(loadPresets, 5000);
  </script>
</body>
</html>
)rawliteral";
}
