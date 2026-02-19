# 🏙️ IoT Smart City
This project is a scalable, multi-tenant IoT ecosystem integrating heterogeneous sensor nodes through a centralized Flask Gateway to ThingsBoard Cloud. This project is part of the NETW503 course at the GUC.
### Included Subprojects
* Automated People Counter Using Ultrasonic Sensors
* Smart Door State Monitoring & Breach Detector
* Smart Parking Availability System
* Smart Motion-Activated Security System (PIR)
* Fallen Object Detection (Gyroscope / MPU6050)


## 🏗️ System Architecture
This project implements a **Centralized Gateway Pattern**. Instead of edge devices connecting directly to the cloud, they communicate via a local hub that manages identity and security.



* **Edge Layer:** Various ESP32-based nodes (People Counters, Parking Sensors, Environment Monitors).
* **Gateway Layer:** A Python Flask server providing dynamic device provisioning, multithreaded request handling, and JWT-based authentication.
* **Cloud Layer:** A unified ThingsBoard instance for cross-project data visualization.

## 📡 Unified Communication Protocol
To ensure compatibility across different teams, all devices must POST a standardized JSON packet to `/api/telemetry`:

```json
{
  "project_id": "TeamName_ProjectName",
  "timestamp": "12345678",
  "data": {
    "sensor_1": 25.5,
    "sensor_2": 10
  }
}
```
## 🛠️ Key Components
All subprojects use a LilyGO T3 (ESP32) board

### 1. Automated People Counter  
**Hardware:** Dual HC-SR04 Ultrasonic Sensors.
**Logic:** Directional sequence detection (Entry/Exit) with real-time OLED feedback.

### 2. Smart Door State Monitoring & Breach Detector
**Hardware:** Smart Door Sensor, LED, Buzzer.
**Logic:** 

### 3. Smart Parking Availability System
**Hardware:** 
**Logic:** 

### 4. Smart Motion-Activated Security System
**Hardware:** HC-SR04 Ultrasonic Sensor.
**Logic:** 

### 5. Fallen Object Detection
**Hardware:** MPU6050 Gyroscope/Accelerometer 
**Logic:** 

### Unified Python Gateway
**Dynamic Provisioning:** Automatically creates new device entities on ThingsBoard using the REST API if a new project_id is detected.

**Thread Safety:** Utilizes threading.Lock to ensure data integrity during simultaneous uploads from multiple devices.

**Persistence:** Maintains a device_mapping.json to link local Project IDs to Cloud Access Tokens.

## 🚀 Getting Started

### Gateway Installation
1. Clone the repository.

2. Install dependencies: ``pip install flask requests``.

3. Create a file named ``.env`` in the same directory as ``gateway_unified.py`` and enter your ThingsBoard account credentials using the following format:
```
TB_USERNAME=username
TB_PASSWORD=password
```

4. Run the server: ``python gateway_unified.py``.

### Connecting a New Node
1. Implement the standard JSON structure in your firmware.

2. Direct your HTTP POST requests to the Gateway's IP address on port ``5000``.

3. The gateway will automatically register your device on the cloud upon the first successful transmission.
