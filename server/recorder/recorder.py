from __future__ import annotations

import logging
import os
import time

import paho.mqtt.client as mqtt

from protocol import VoiceAssembler, save_recording

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
assembler = VoiceAssembler()
recordings_dir = os.environ.get("RECORDINGS_DIR", "/recordings")
topic = os.environ.get("MQTT_TOPIC", "echolink/v1/voice")


def on_connect(client: mqtt.Client, userdata, flags, reason_code, properties) -> None:
    if reason_code != 0:
        logging.error("MQTT connection failed: %s", reason_code)
        return
    client.subscribe(topic, qos=0)
    logging.info("subscribed to %s", topic)


def on_message(client: mqtt.Client, userdata, message: mqtt.MQTTMessage) -> None:
    try:
        completed = assembler.feed(message.payload)
        if completed is not None:
            path = save_recording(completed, recordings_dir)
            logging.info("saved %s", path)
    except ValueError as error:
        logging.warning("ignored malformed frame: %s", error)


def main() -> None:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="echolink-recorder")
    user = os.environ.get("MQTT_USER", "")
    password = os.environ.get("MQTT_PASSWORD", "")
    if user:
        client.username_pw_set(user, password)
    client.on_connect = on_connect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    client.connect_async(os.environ.get("MQTT_HOST", "mqtt"),
                         int(os.environ.get("MQTT_PORT", "1883")), 60)
    client.loop_start()
    try:
        while True:
            assembler.discard_stale()
            time.sleep(5)
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
