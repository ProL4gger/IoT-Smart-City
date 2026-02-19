from flask import Flask, request, jsonify
import requests
import json
import os
import csv
from datetime import datetime, timedelta
import threading
from dotenv import load_dotenv

app = Flask(__name__)

# ==================== CONFIG ====================

THINGSBOARD_URL = "https://thingsboard.cloud"

MAPPING_JSON = "device_mapping.json"
MAPPING_CSV = "device_mapping.csv"

# Load the ".env" file containing ThingsBoard account credentials. Make sure to create this file with the following format:
# TB_USERNAME=username
# TB_USERNAME=password
load_dotenv()

# Read the variables
TB_USERNAME = os.getenv("TB_USERNAME")
TB_PASSWORD = os.getenv("TB_PASSWORD")

# Safety check (Optional but recommended)
if not TB_USERNAME or not TB_PASSWORD:
    raise ValueError("[ERROR] Credentials not found. Check if .env credentials file was created")
    
jwt_token = None
token_expiry = None

# Thread safety
mapping_lock = threading.Lock()
jwt_lock = threading.Lock()

# ==================== JWT TOKEN ====================

def get_fresh_token():
    global jwt_token, token_expiry

    with jwt_lock:
        # Check again inside lock (double-check locking)
        if jwt_token is not None and token_expiry is not None and datetime.now() < token_expiry:
            return True

        url = f"{THINGSBOARD_URL}/api/auth/login"
        payload = {
            "username": TB_USERNAME,
            "password": TB_PASSWORD
        }

        print("[INFO] Requesting JWT from ThingsBoard...")
        try:
            r = requests.post(url, json=payload, timeout=10)

            if r.status_code != 200:
                print("[ERROR] JWT login failed:", r.text)
                return False

            data = r.json()
            jwt_token = "Bearer " + data["token"]
            token_expiry = datetime.now() + timedelta(hours=7)

            print("[OK] JWT token obtained")
            return True
        except Exception as e:
            print(f"[ERROR] JWT request exception: {e}")
            return False


def ensure_valid_token():
    if jwt_token is None or token_expiry is None or datetime.now() >= token_expiry:
        return get_fresh_token()
    return True

# ==================== MAPPING ====================

def load_mapping():
    if not os.path.exists(MAPPING_JSON):
        return {}
    try:
        with open(MAPPING_JSON, "r") as f:
            return json.load(f)
    except Exception as e:
        print(f"[ERROR] Failed to load mapping: {e}")
        return {}


def save_mapping(mapping):
    try:
        # JSON (logic)
        with open(MAPPING_JSON, "w") as f:
            json.dump(mapping, f, indent=4)

        # CSV (readability)
        with open(MAPPING_CSV, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["project_id", "access_token"])
            for pid, token in mapping.items():
                writer.writerow([pid, token])
        
        print(f"[OK] Mapping saved ({len(mapping)} devices)")
    except Exception as e:
        print(f"[ERROR] Failed to save mapping: {e}")

# ==================== THINGSBOARD DEVICE ====================

def create_device(project_id):
    if not ensure_valid_token():
        return None

    headers = {
        "Content-Type": "application/json",
        "X-Authorization": jwt_token
    }

    # Create device
    create_url = f"{THINGSBOARD_URL}/api/device"
    payload = {
        "name": project_id,
        "type": "SmartCityDevice"
    }

    print(f"[INFO] Creating device '{project_id}'...")
    try:
        r = requests.post(create_url, json=payload, headers=headers, timeout=10)

        if r.status_code != 200:
            print("[ERROR] Device creation failed:", r.text)
            return None

        device_id = r.json()["id"]["id"]

        # Get credentials (access token)
        cred_url = f"{THINGSBOARD_URL}/api/device/{device_id}/credentials"
        r = requests.get(cred_url, headers=headers, timeout=10)

        if r.status_code != 200:
            print("[ERROR] Failed to get credentials")
            return None

        token = r.json()["credentialsId"]
        print("[OK] Device provisioned:", project_id)

        return token
    except Exception as e:
        print(f"[ERROR] Device creation exception: {e}")
        return None

# ==================== PROVISIONING (THREAD-SAFE) ====================

def get_or_provision(project_id):
    # Thread-safe device provisioning
    with mapping_lock:
        mapping = load_mapping()

        if project_id in mapping:
            print(f"[INFO] Existing device '{project_id}' reused")
            return mapping[project_id]

        print(f"[INFO] New project '{project_id}' -> provisioning")
        token = create_device(project_id)

        if token is None:
            return None

        mapping[project_id] = token
        save_mapping(mapping)
        return token

# ==================== TELEMETRY ====================

def forward_telemetry(token, data):
    url = f"{THINGSBOARD_URL}/api/v1/{token}/telemetry"

    try:
        r = requests.post(url, json=data, timeout=5)

        if r.status_code == 200:
            print("[OK] Telemetry forwarded to ThingsBoard")
            return True

        print("[ERROR] Telemetry failed:", r.status_code, r.text)
        return False
    except Exception as e:
        print(f"[ERROR] Telemetry exception: {e}")
        return False

# ==================== API ====================

@app.route("/api/telemetry", methods=["POST"])
def receive_telemetry():
    try:
        packet = request.json

        if not packet:
            return jsonify({"error": "No JSON received"}), 400

        project_id = packet.get("project_id")
        data = packet.get("data")

        if not project_id or not data:
            return jsonify({"error": "Missing project_id or data"}), 400

        print("\n" + "=" * 60)
        print(f"PROJECT: {project_id}")
        print(f"DATA: {json.dumps(data, indent=2)}")
        print("=" * 60)

        token = get_or_provision(project_id)
        if token is None:
            return jsonify({"error": "Provisioning failed"}), 500

        if not forward_telemetry(token, data):
            return jsonify({"error": "Telemetry forwarding failed"}), 500

        return jsonify({"status": "success"}), 200
    
    except Exception as e:
        print(f"[ERROR] Request handling exception: {e}")
        return jsonify({"error": "Internal server error"}), 500


@app.route("/api/devices", methods=["GET"])
def list_devices():
    try:
        with mapping_lock:
            mapping = load_mapping()
        return jsonify(mapping), 200
    except Exception as e:
        print(f"[ERROR] Failed to list devices: {e}")
        return jsonify({"error": "Failed to load devices"}), 500


@app.route("/health", methods=["GET"])
def health_check():
    """Health check endpoint for monitoring"""
    return jsonify({
        "status": "healthy",
        "jwt_valid": jwt_token is not None and token_expiry is not None and datetime.now() < token_expiry,
        "timestamp": datetime.now().isoformat()
    }), 200


@app.route("/telemetry", methods=["POST"])
def receive_telemetry_simple():
    """Compatibility endpoint for other groups using /telemetry format"""
    return receive_telemetry()

# ==================== MAIN ====================

if __name__ == "__main__":
    print("\n -----------------------------------")
    print("  SMART CITY GATEWAY SERVER         ")
    print("  Multi-Device Support Enabled      ")
    print("-------------------------------------")
    
    if not get_fresh_token():
        print("[WARNING] JWT not obtained — provisioning will fail")
    else:
        print("[OK] Server ready to accept connections")
    
    print("\n[INFO] Endpoints:")
    print("  POST /api/telemetry  - Receive device data")
    print("  GET  /api/devices    - List all devices")
    print("  GET  /health         - Health check")
    print("\n[INFO] Starting server on http://0.0.0.0:5000")
    print("-" * 50 + "\n")

    app.run(host="0.0.0.0", port=5000, debug=True, threaded=True)