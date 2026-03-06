import os
import time
import json
import threading
from queue import Queue

# third-party libraries
import cv2
import requests
import paho.mqtt.client as mqtt
from ultralytics import YOLO


# --- Configuration -----------------------------------------------------------
MODEL_PATH = "22004068_1.pt"
API_URL = "http://localhost/IoT4/api/save_detection.php"
MQTT_BROKER = "broker.emqx.io"
MQTT_TOPIC = "esp32/traffic/sign"
SAVE_DIR = "detection_results"

# Classes used by the model (aligned with training labels)
CLASS_NAMES = ["Glass", "Metal", "Paper", "Plastic", "Waste"]

# Mapping from YOLO index to database category ID (update as needed)
CLASS_ID_MAP = {
    0: 1,  # Glass
    1: 2,  # Metal
    2: 3,  # Paper
    3: 4,  # Plastic
    4: 5,  # Waste
}


# --- Globals ------------------------------------------------------------------
notify_queue = Queue()


# --- MQTT helpers ------------------------------------------------------------


def init_mqtt_client():
    """Create and return a connected MQTT client with a running network loop."""
    client = mqtt.Client()
    client.connect(MQTT_BROKER, 1883, 60)
    client.loop_start()
    return client


mqtt_client = init_mqtt_client()


# --- Notification worker -----------------------------------------------------


def notification_worker():
    """Background thread that handles database/API logging and MQTT notifications."""
    print("Notification worker started.")
    while True:
        item = notify_queue.get()
        if item is None:  # sentinel used to shut down the thread
            break

        category_id, confidence = item
        try:
            print(f"[Thread] Calling API for category {category_id}...")
            resp = requests.get(
                API_URL,
                params={"sign_id": category_id, "confidence": confidence},
                timeout=5,
            )
            data = resp.json()

            if data.get("status") == "success":
                name = data["data"]["name"]
                name_type = data["data"]["name_type"]

                payload = {
                    "name": name,
                    "type": name_type,
                    "conf": round(confidence * 100, 1),
                }
                mqtt_client.publish(MQTT_TOPIC, json.dumps(payload))
                print(f"[Thread] MQTT Sent: {name} ({payload['conf']}%)")
            else:
                print(f"[Thread] API Error: {data.get('message')}")
        except Exception as exc:
            print(f"[Thread] Notification error: {exc}")
        finally:
            notify_queue.task_done()


def start_notification_worker():
    """Boot the notification worker thread and return the Thread object."""
    thread = threading.Thread(target=notification_worker, daemon=True)
    thread.start()
    return thread


# --- Model and detection -----------------------------------------------------

# load once to avoid repeated overhead
model = YOLO(MODEL_PATH)


def detect_and_notify(image_path: str) -> None:
    """Run inference on an image and queue any detections for notification."""
    print(f"Running inference on {image_path}")
    results = model(image_path)

    os.makedirs(SAVE_DIR, exist_ok=True)

    for idx, result in enumerate(results):
        annotated = result.plot()
        output_path = os.path.join(SAVE_DIR, f"result_{idx}.jpg")
        cv2.imwrite(output_path, annotated)
        print(f"Annotated image saved: {output_path}")

        if not result.boxes:
            print("No objects detected.")
            continue

        for box in result.boxes:
            cls = int(box.cls.item())
            conf = float(box.conf.item())
            label = CLASS_NAMES[cls] if cls < len(CLASS_NAMES) else f"Unknown({cls})"
            print(f"Detected {label} (conf {conf:.2f})")

            if cls in CLASS_ID_MAP:
                notify_queue.put((CLASS_ID_MAP[cls], conf))
                print(f"Queued notification for category {CLASS_ID_MAP[cls]}")
            else:
                print(f"No mapping for detected class {label}")


# --- Entry point -------------------------------------------------------------


def main():
    worker = start_notification_worker()

    # example/test usage
    test_img = r"Test-files/Test1.jpg"
    if os.path.exists(test_img):
        print(f"Testing with {test_img}")
        detect_and_notify(test_img)
        notify_queue.join()
        time.sleep(1)
    else:
        print(f"Test file not found: {test_img}")

    # clean up
    mqtt_client.loop_stop()
    mqtt_client.disconnect()
    # signal worker to stop
    notify_queue.put(None)
    worker.join(timeout=1)
    print("Exiting.")


if __name__ == "__main__":
    main()
