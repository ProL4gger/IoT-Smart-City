#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==================== OLED SETUP ====================
#define SCREEN_WIDTH  128        //lilygo parameters
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== SENSOR PINS ====================
const int trigPin = 12; //pins used
const int echoPin = 14;

// ==================== GLOBAL VARIABLES ====================
WebServer configServer(80); // Web server on port 80
Preferences prefs;   // For saving settings

// Configuration settings
String ssid = "";
String password = "";
String projectID = "";
String serverIP = "";
int serverPort = 5000;
int uploadInterval = 5000;  // milliseconds

// State management
bool hasValidSettings = false;
bool userAccessedPage = false;
unsigned long configModeStartTime = 0;
const unsigned long CONFIG_TIMEOUT = 30000; //Enforce 30-second config timeout
bool inConfigMode = true; 
bool timeoutRunning = false;
unsigned long timeoutStart = 0;

// ==================== ULTRASONIC SENSOR ====================
float readDistanceCM() {
  digitalWrite(trigPin, LOW); //This ensures the trigger pin is in a clean state before we start
  delayMicroseconds(2); //Waits 2 microseconds
  digitalWrite(trigPin, HIGH); 
  delayMicroseconds(10); 
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000UL); //pulsein measures how long echo stays high
  if (duration <= 0) return -1; //if duration is -1 or 0 something went wrong

  return (duration * 0.0343) / 2.0;
}

// ==================== CONFIGURATION WEBPAGE ====================
String configPage(String message = "",
                  String f_ssid="", String f_pass="", String f_projectID="",
                  String f_serverIP="", String f_serverPort="", 
                  String f_interval="", int remainingSeconds=-1) {
  String html = R"(
  <html>
  <head>
    <title>Smart Parking Configuration</title>
    <style>
      body { font-family: Arial; padding: 20px; }
      input { width: 250px; padding: 6px; margin: 5px; }
      label { font-weight: bold; }
      button { padding: 10px 20px; margin-top: 15px; }
      p { color: red; }
    </style>
  </head>
  <body>
    <h2>Smart Parking - Configuration</h2>
  )";
  
  if(message != "") html += "<p><b>" + message + "</b></p>"; //if message not empty
  
  if(remainingSeconds >= 0) { //
    html += "<p id='timer'><b>Time remaining: " + String(remainingSeconds) + " seconds</b></p>";
    html += R"(
    <script>
      var sec = )" + String(remainingSeconds) + R"(;
      var t = setInterval(function(){
        sec--;
        if(sec <= 0){
          document.getElementById('timer').innerHTML = "<b>Time is up!</b>";
          clearInterval(t);
        } else {
          document.getElementById('timer').innerHTML =
            "<b>Time remaining: " + sec + " seconds</b>";
        }
      },1000);
    </script>
    )";
  }
  
  html += "<form method='POST' action='/save'>";
  html += "<label>Project ID (e.g., SmartParking_Team7):</label><br><input name='project_id' value='"+f_projectID+"' required><br>";
  html += "<label>WiFi SSID:</label><br><input name='ssid' value='"+f_ssid+"' required><br>";
  html += "<label>WiFi Password:</label><br><input name='password' type='password' value='"+f_pass+"' required><br>";
  html += "<label>Gateway Server IP:</label><br><input name='server_ip' value='"+f_serverIP+"' placeholder='192.168.1.19' required><br>";
  html += "<label>Gateway Port:</label><br><input name='server_port' type='number' value='"+f_serverPort+"' required><br>";
  html += "<label>Upload Interval (ms):</label><br><input name='interval' type='number' value='"+f_interval+"' required><br>";
  html += "<button type='submit'>Save Settings</button></form></body></html>";
  return html;
}

// ==================== SAVE HANDLER ====================
void handleSave() {
  String f_projectID = configServer.arg("project_id");
  String f_ssid = configServer.arg("ssid");
  String f_pass = configServer.arg("password");
  String f_serverIP = configServer.arg("server_ip");
  String f_serverPort = configServer.arg("server_port");
  String f_interval = configServer.arg("interval");
  
  bool empty = (f_projectID == "" || f_ssid == "" || f_pass == "" || 
                f_serverIP == "" || f_serverPort == "" || f_interval == "");
  
  if(empty) {
    if(!timeoutRunning) timeoutStart = millis();
    timeoutRunning = true;
    unsigned long elapsed = millis() - timeoutStart;
    int remaining = 60 - (elapsed / 1000);
    if(remaining < 0) remaining = 0;
    
    if(elapsed > 60000) {
      configServer.send(200,"text/html", configPage(
        "Failed to save. Still in configuration mode.", 
        f_ssid, f_pass, f_projectID, f_serverIP, f_serverPort, f_interval, -1));
      timeoutRunning = false;
    } else {
      configServer.send(200,"text/html", configPage(
        "All fields required! Complete within 1 minute.", 
        f_ssid, f_pass, f_projectID, f_serverIP, f_serverPort, f_interval, remaining));
    }
    return;
  }
  
  prefs.begin("config", false);
  prefs.putString("projectID", f_projectID);
  prefs.putString("ssid", f_ssid);
  prefs.putString("password", f_pass);
  prefs.putString("serverIP", f_serverIP);
  prefs.putInt("serverPort", f_serverPort.toInt());
  prefs.putInt("interval", f_interval.toInt());
  prefs.end();
  
  configServer.send(200,"text/html","<h2>Settings Saved!</h2><p>Restarting in 5 seconds...</p>");
  
  display.clearDisplay(); 
  display.setCursor(0,0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("Settings Saved!");
  display.println("Project: " + f_projectID);
  display.println("Restarting...");
  display.display();
  
  Serial.println("\n[INFO] Settings saved, restarting...");
  delay(5000);
  ESP.restart();
}

// ==================== LOAD SETTINGS ====================
bool loadSettings() {
  prefs.begin("config", true);
  projectID = prefs.getString("projectID","");
  ssid = prefs.getString("ssid","");
  password = prefs.getString("password","");
  serverIP = prefs.getString("serverIP","");
  serverPort = prefs.getInt("serverPort", 5000);
  uploadInterval = prefs.getInt("interval", 5000);
  prefs.end();
  
  return !(projectID=="" || ssid=="" || password=="" || serverIP=="");
}

// ==================== SEND TELEMETRY ====================
bool sendTelemetry(float distance, String spotStatus) {
  // Create JSON payload
  String payload = "{";
  payload += "\"project_id\":\"" + projectID + "\",";
  payload += "\"timestamp\":\"" + String(millis()) + "\",";
  payload += "\"data\":{";
  payload += "\"distance\":" + String(distance, 2) + ",";
  payload += "\"status\":\"" + spotStatus + "\"";
  payload += "}}";
  
  // Send to server endpoint: /telemetry
  HTTPClient http;
  String url = "http://" + serverIP + ":" + String(serverPort) + "/telemetry";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  
  int httpCode = http.POST(payload);
  String response = http.getString();
  http.end();
  
  // Log result
  Serial.println("=====================================");
  Serial.printf("[TELEMETRY] Distance: %.2f cm | Status: %s\n", distance, spotStatus.c_str());
  Serial.println("URL: " + url);
  Serial.println("Payload: " + payload);
  Serial.println("HTTP Code: " + String(httpCode));
  if(httpCode == 200) {
    Serial.println("[OK] Success!");
    Serial.println("Response: " + response);
  } else {
    Serial.println("[ERROR] Failed!");
  }
  Serial.println("=====================================\n");
  
  return (httpCode == 200);
}

// ==================== NORMAL MODE ====================
void normalMode() {
  display.clearDisplay(); 
  display.setCursor(0,0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("Connecting WiFi...");
  display.println(ssid);
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  int attempts = 0;
  while(WiFi.status() != WL_CONNECTED && attempts < 40) { 
    delay(500); 
    attempts++; 
  }

  if(WiFi.status() != WL_CONNECTED) {
    display.clearDisplay(); 
    display.setCursor(0,0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.println("WiFi Failed!");
    display.println("Check credentials");
    display.display();
    while(true) delay(1000);
  }

  display.clearDisplay(); 
  display.setCursor(0,0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("WiFi Connected!");
  display.println(WiFi.localIP().toString());
  display.println("");
  display.println("Project: " + projectID);
  display.println("Gateway: " + serverIP);
  display.display();
  delay(3000);

  Serial.println("\n========== GATEWAY MODE ==========");
  Serial.println("Project ID: " + projectID);
  Serial.println("Gateway: " + serverIP + ":" + String(serverPort));
  Serial.println("Endpoint: /telemetry");
  Serial.println("===================================\n");

  // Initialize sensor pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  unsigned long lastSend = 0;
  unsigned long lastDisplayUpdate = 0;

  // Main loop
  while(true) {
    // Read distance from ultrasonic sensor
    float distance = readDistanceCM();
    
    // Determine parking spot status
    String spotStatus;
    if (distance <= 10 && distance > 0) {
      spotStatus = "Unavailable";
    } else {
      spotStatus = "Available";
    }

    // Update OLED display
    if(millis() - lastDisplayUpdate >= 200) {
      lastDisplayUpdate = millis();
      display.clearDisplay(); 
      display.setCursor(0,0);
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.println("== SMART PARKING ==");
      display.println("");
      display.print("Distance: ");
      if(distance < 0) {
        display.println("Error");
      } else {
        display.print(distance, 1);
        display.println(" cm");
      }
      display.println("");
      display.print("Status: ");
      display.println(spotStatus);
      display.println("");
      display.println("Gateway: " + serverIP);
      unsigned long next = uploadInterval - (millis() - lastSend);
      if(next > uploadInterval) next = 0;
      display.print("Next TX: "); 
      display.print(next/1000); 
      display.println("s");
      display.display();
    }

    // Send telemetry
    if(millis() - lastSend >= uploadInterval) {
      lastSend = millis();
      sendTelemetry(distance, spotStatus);
    }
    
    delay(1);
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200); 
  delay(1000);
  Serial.println("\n\n=== SMART PARKING - SERVER MODE ===");
  Serial.println("Compatible with /telemetry endpoint");
  Serial.println("Booting...");

  Wire.begin(12, 14);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[ERROR] OLED failed!");
  } else {
    Serial.println("[OK] OLED initialized");
  }

  hasValidSettings = loadSettings();
  Serial.println(hasValidSettings ? "[OK] Valid settings found" : "[INFO] First boot");

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Smart_Parking", "12345678");

  configServer.on("/", [](){ 
    userAccessedPage = true; 
    configServer.send(200, "text/html", configPage()); 
  });
  configServer.on("/save", HTTP_POST, handleSave);
  configServer.begin();

  display.clearDisplay(); 
  display.setCursor(0,0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("CONFIG MODE");
  display.println("SSID: Smart_Parking");
  display.println("PASS: 12345678");
  display.println("IP: 192.168.4.1");
  if(hasValidSettings) display.println("Auto-start: 30s");
  display.display();

  Serial.println("AP: Smart_Parking (12345678)");
  Serial.println("Config: http://192.168.4.1");

  configModeStartTime = millis();
}

// ==================== LOOP ====================
void loop() {
  if(inConfigMode) {
    configServer.handleClient();
    
    if(hasValidSettings && !userAccessedPage && millis() - configModeStartTime >= CONFIG_TIMEOUT) {
      Serial.println("\n[INFO] 30s timeout - starting normal mode");
      inConfigMode = false;
      configServer.stop();
      WiFi.softAPdisconnect(true);
    }
  } else {
    normalMode();
  }
}
