#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>

// ────────────────── Pin Definitions ──────────────────
#define MPU_SDA       13
#define MPU_SCL       14
#define BUZZER        25

// OLED definitions
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C

// ────────────────── Global Objects ──────────────────
TwoWire I2C_MPU = TwoWire(1);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ────────────────── Variables ──────────────────
Preferences preferences;
WebServer configServer(80);

String projectID = "";
String ssid = "";
String password = "";
String serverIP = "";
int uploadInterval = 10;           // seconds

bool inConfigMode = true;
bool hasValidSettings = false;
bool userAccessedPage = false;
bool fallDetectedFlag = false;

int fallCount = 0;
unsigned long lastUploadTime = 0;
unsigned long configModeStartTime = 0;
unsigned long timeoutStart = 0;
bool timeoutRunning = false;

const unsigned long CONFIG_TIMEOUT = 30000; // 30 seconds

// Previous accelerometer values
int16_t prev_AcX = 0, prev_AcY = 0, prev_AcZ = 0;

// Spike detection threshold
const int16_t spikeThreshold = 21750;

// Buzzer pattern control
bool doPattern = false;
unsigned long patternStepTime = 0;
int patternStep = 0;

// ────────────────── Function Prototypes ──────────────────
bool loadSettings();
String configPage(String message = "", String f_projectID = "", String f_ssid = "",
                  String f_pass = "", String f_serverIP = "", String f_interval = "",
                  int remainingSeconds = -1);

// ────────────────── SETTINGS LOAD ──────────────────
bool loadSettings() {
    preferences.begin("config", true);
    projectID     = preferences.getString("projectID", "");
    ssid          = preferences.getString("ssid", "");
    password      = preferences.getString("password", "");
    serverIP      = preferences.getString("serverIP", "");
    uploadInterval = preferences.getInt("interval", 10);
    preferences.end();

    return !(projectID == "" || ssid == "" || password == "" || serverIP == "");
}

// ────────────────── HTML CONFIG PAGE ──────────────────
String configPage(String message, String f_projectID, String f_ssid,
                  String f_pass, String f_serverIP, String f_interval,
                  int remainingSeconds) {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Fallen Object Detection Config</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, Helvetica, sans-serif; padding: 20px; max-width: 500px; margin: auto; }
    label { display: block; margin: 12px 0 4px; font-weight: bold; }
    input { width: 100%; padding: 8px; box-sizing: border-box; }
    button { padding: 12px 24px; margin-top: 20px; background: #4CAF50; color: white; border: none; cursor: pointer; }
    button:hover { background: #45a049; }
    .error { color: #d32f2f; font-weight: bold; }
    #timer { color: #e65100; font-weight: bold; }
  </style>
</head>
<body>
  <h2>Fallen Object Detection - Config</h2>
)rawliteral";

    if (message != "") html += "<p class='error'>" + message + "</p>";

    if (remainingSeconds >= 0) {
        html += "<p id='timer'>Time remaining: " + String(remainingSeconds) + " seconds</p>";
        html += "<script>"
                "let sec = " + String(remainingSeconds) + ";"
                "const t = setInterval(() => {"
                "  sec--;"
                "  if (sec <= 0) {"
                "    document.getElementById('timer').innerHTML = '<b>Time is up!</b>';"
                "    clearInterval(t);"
                "  } else {"
                "    document.getElementById('timer').innerHTML = 'Time remaining: ' + sec + ' seconds';"
                "  }"
                "}, 1000);"
                "</script>";
    }

    html += R"rawliteral(
  <form method="POST" action="/save">
    <label>Project ID (e.g. FallenObject_Team9):</label>
    <input name="project_id" value=")rawliteral" + f_projectID + R"rawliteral(" required>

    <label>WiFi SSID:</label>
    <input name="ssid" value=")rawliteral" + f_ssid + R"rawliteral(" required>

    <label>WiFi Password:</label>
    <input name="password" type="password" value=")rawliteral" + f_pass + R"rawliteral(" required>

    <label>Gateway Server IP:</label>
    <input name="server_ip" value=")rawliteral" + f_serverIP + R"rawliteral(" placeholder="192.168.1.100" required>

    <label>Upload Interval (seconds):</label>
    <input name="interval" type="number" min="5" value=")rawliteral" + f_interval + R"rawliteral(" required>

    <button type="submit">Save & Restart</button>
  </form>
</body>
</html>
)rawliteral";

    return html;
}

// ────────────────── SAVE HANDLER ──────────────────
void handleSave() {
    String f_projectID  = configServer.arg("project_id");
    String f_ssid       = configServer.arg("ssid");
    String f_pass       = configServer.arg("password");
    String f_serverIP   = configServer.arg("server_ip");
    String f_interval   = configServer.arg("interval");

    bool anyEmpty = (f_projectID == "" || f_ssid == "" || f_pass == "" ||
                     f_serverIP == "" || f_interval == "");

    if (anyEmpty) {
        if (!timeoutRunning) {
            timeoutStart = millis();
            timeoutRunning = true;
        }

        unsigned long elapsed = millis() - timeoutStart;
        int remaining = 60 - (elapsed / 1000);
        if (remaining < 0) remaining = 0;

        if (elapsed > 60000) {
            configServer.send(200, "text/html", configPage(
                "Failed to save - all fields are required!",
                f_projectID, f_ssid, f_pass, f_serverIP, f_interval, -1));
            timeoutRunning = false;
        } else {
            configServer.send(200, "text/html", configPage(
                "All fields are required! Please complete within 1 minute.",
                f_projectID, f_ssid, f_pass, f_serverIP, f_interval, remaining));
        }
        return;
    }

    // Save settings
    preferences.begin("config", false);
    preferences.putString("projectID", f_projectID);
    preferences.putString("ssid",      f_ssid);
    preferences.putString("password",  f_pass);
    preferences.putString("serverIP",  f_serverIP);
    preferences.putInt("interval",     f_interval.toInt());
    preferences.end();

    configServer.send(200, "text/html",
                      "<h2>Settings Saved!</h2><p>Device will restart in 5 seconds...</p>");

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Settings Saved!");
    display.println("Project: " + f_projectID);
    display.println("Restarting...");
    display.display();

    Serial.println("\n[INFO] Settings saved → restarting in 5s...");
    delay(5000);
    ESP.restart();
}

// ────────────────── SETUP ──────────────────
void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println("\n\n=== FALLEN OBJECT DETECTION - M4 ===\n");

    pinMode(BUZZER, OUTPUT);
    digitalWrite(BUZZER, LOW);

    // OLED initialization
    Wire.begin(21, 22);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[ERROR] SSD1306 allocation failed");
    } else {
        Serial.println("[OK] OLED initialized");
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Load saved settings
    hasValidSettings = loadSettings();
    Serial.println(hasValidSettings ? "[OK] Valid settings loaded" : "[INFO] No valid settings found");

    // Start Access Point for configuration
    WiFi.mode(WIFI_AP);
    WiFi.softAP("LilyGO_CONFIG", "12345678");

    // Web server routes
    configServer.on("/", []() {
        userAccessedPage = true;
        configServer.send(200, "text/html", configPage());
    });

    configServer.on("/save", HTTP_POST, handleSave);
    configServer.begin();

    // Show config information on display
    display.clearDisplay();
    display.setCursor(0, 5);
    display.println("CONFIG MODE");
    display.println("SSID: LilyGO_CONFIG");
    display.println("PASS: 12345678");
    display.println("IP:  192.168.4.1");
    if (hasValidSettings) display.println("Auto-start in 30s");
    display.display();

    Serial.println("AP Started →  LilyGO_CONFIG / 12345678");
    Serial.println("Config page →  http://192.168.4.1");

    // Wake up MPU6050
    I2C_MPU.begin(MPU_SDA, MPU_SCL, 400000);
    I2C_MPU.beginTransmission(0x68);
    I2C_MPU.write(0x6B);  // Power management register
    I2C_MPU.write(0x00);  // Wake up
    I2C_MPU.endTransmission(true);

    // Startup beep
    digitalWrite(BUZZER, HIGH);
    delay(150);
    digitalWrite(BUZZER, LOW);

    Serial.println("[OK] MPU6050 initialized");

    configModeStartTime = millis();
}

// ────────────────── LOOP ──────────────────
void loop() {
    if (inConfigMode) {
        configServer.handleClient();

        // Auto-exit config mode after timeout if settings exist
        if (hasValidSettings && !userAccessedPage &&
            (millis() - configModeStartTime >= CONFIG_TIMEOUT)) {
            Serial.println("[INFO] 30s timeout → switching to normal mode");
            inConfigMode = false;
            configServer.stop();
            WiFi.softAPdisconnect(true);

            WiFi.mode(WIFI_STA);
            WiFi.begin(ssid.c_str(), password.c_str());
            Serial.println("[INFO] Connecting to WiFi: " + ssid);
        }
        return;
    }

    // Wait for WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastAttempt = 0;
        if (millis() - lastAttempt > 1500) {
            display.clearDisplay();
            display.setCursor(0, 10);
            display.println("Connecting WiFi...");
            display.print("SSID: ");
            display.println(ssid.substring(0, 14));
            display.display();
            Serial.print(".");
            lastAttempt = millis();
        }
        delay(100);
        return;
    }

    // Show connection success once
    static bool wifiShown = false;
    if (!wifiShown) {
        Serial.println("\n[OK] WiFi Connected!");
        Serial.print("[INFO] IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.println("[INFO] Starting sensor loop...");
        wifiShown = true;
    }

    // ─── Read MPU6050 ───────────────────────────────────────
    I2C_MPU.beginTransmission(0x68);
    I2C_MPU.write(0x3B);
    I2C_MPU.endTransmission(false);
    I2C_MPU.requestFrom(0x68, 14, true);

    int16_t AcX = I2C_MPU.read() << 8 | I2C_MPU.read();
    int16_t AcY = I2C_MPU.read() << 8 | I2C_MPU.read();
    int16_t AcZ = I2C_MPU.read() << 8 | I2C_MPU.read();
    for (int i = 0; i < 8; i++) I2C_MPU.read();  // skip temp + gyro

    // ─── Fall / Spike Detection ─────────────────────────────
    int16_t dx = abs(AcX - prev_AcX);
    int16_t dy = abs(AcY - prev_AcY);
    int16_t dz = abs(AcZ - prev_AcZ);

    if (!doPattern && (dx > spikeThreshold || dy > spikeThreshold || dz > spikeThreshold)) {
        Serial.println("!!! SPIKE DETECTED !!! → Starting buzzer pattern");
        fallDetectedFlag = true;
        fallCount++;
        doPattern = true;
        patternStep = 0;
        patternStepTime = millis();
    }

    // ─── Update Display (every 200ms) ───────────────────────
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate >= 200) {
        lastDisplayUpdate = millis();

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.print(projectID.substring(0, 15));
        display.setCursor(0, 12);
        display.print("X: "); display.println(AcX);
        display.setCursor(0, 22);
        display.print("Y: "); display.println(AcY);
        display.setCursor(0, 32);
        display.print("Z: "); display.println(AcZ);
        display.setCursor(0, 42);
        display.print("Falls: "); display.println(fallCount);
        display.setCursor(0, 54);
        display.print(WiFi.localIP());
        display.display();
    }

    // ─── Buzzer Pattern Sequence ────────────────────────────
    if (doPattern) {
        unsigned long now = millis();
        switch (patternStep) {
            case 0: digitalWrite(BUZZER, HIGH);
                    if (now - patternStepTime > 150) {
                        digitalWrite(BUZZER, LOW);
                        patternStep++; patternStepTime = now;
                    } break;
            case 1: if (now - patternStepTime > 100) {
                        patternStep++; patternStepTime = now;
                    } break;
            case 2: digitalWrite(BUZZER, HIGH);
                    if (now - patternStepTime > 150) {
                        digitalWrite(BUZZER, LOW);
                        patternStep++; patternStepTime = now;
                    } break;
            case 3: if (now - patternStepTime > 120) {
                        patternStep++; patternStepTime = now;
                    } break;
            case 4: digitalWrite(BUZZER, HIGH);
                    if (now - patternStepTime > 400) {
                        digitalWrite(BUZZER, LOW);
                        patternStep++; patternStepTime = now;
                    } break;
            case 5:
            default:
                    doPattern = false;
                    digitalWrite(BUZZER, LOW);
                    Serial.println("Buzzer pattern finished");
                    break;
        }
    }

    prev_AcX = AcX;
    prev_AcY = AcY;
    prev_AcZ = AcZ;

    // ─── Telemetry Upload ───────────────────────────────────
    if (millis() - lastUploadTime >= (unsigned long)uploadInterval * 1000UL) {
        lastUploadTime = millis();

        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            String url = "http://" + serverIP + ":5000/telemetry";

            http.begin(url);
            http.addHeader("Content-Type", "application/json");

            String json = "{";
            json += "\"project_id\":\"" + projectID + "\",";
            json += "\"timestamp\":\"" + String(millis()) + "\",";
            json += "\"data\":{";
            json += "\"accel_x\":" + String(AcX) + ",";
            json += "\"accel_y\":" + String(AcY) + ",";
            json += "\"accel_z\":" + String(AcZ) + ",";
            json += "\"fall_detected\":" + String(fallDetectedFlag ? 1 : 0) + ",";
            json += "\"fall_count\":" + String(fallCount);
            json += "}}";

            // Clean logging with separator
            Serial.println("\n" + String(60, '='));
            Serial.println("Sending telemetry to: " + url);
            Serial.println("Payload: " + json);

            int httpCode = http.POST(json);

            if (httpCode > 0) {
                Serial.print("HTTP Response: " + String(httpCode));
                if (httpCode == 200) Serial.println("  [OK]");
                else                 Serial.println("  [FAILED]");
            } else {
                Serial.printf("POST failed → error: %s (code: %d)\n",
                              http.errorToString(httpCode).c_str(), httpCode);
            }

            Serial.println(String(60, '=') + "\n");

            http.end();

            // Reset flag AFTER sending
            fallDetectedFlag = false;
        } else {
            Serial.println("[WARNING] WiFi not connected - skipping telemetry");
        }
    }

    delay(30);
}
