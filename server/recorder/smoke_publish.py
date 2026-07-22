import os
import struct
import time

import paho.mqtt.client as mqtt

header = struct.Struct("<HBBIIHHIH")
topic = os.environ.get("MQTT_TOPIC", "echolink/v1/voice")
sender = 0xA11CE001
session = 0x1234
voice = bytes((index % 256 for index in range(16000)))


def frame(packet_type, sequence=0, payload=b""):
    return header.pack(0xEC71, 2, packet_type, 0x4D3A91C7, sender, session,
                       sequence, len(voice), len(payload)) + payload


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="echolink-smoke-test")
client.username_pw_set(os.environ["MQTT_USER"], os.environ["MQTT_PASSWORD"])
client.connect(os.environ.get("MQTT_HOST", "mqtt"), int(os.environ.get("MQTT_PORT", "1883")), 60)
client.loop_start()
client.publish(topic, frame(1)).wait_for_publish()
for sequence, offset in enumerate(range(0, len(voice), 200)):
    client.publish(topic, frame(2, sequence, voice[offset:offset + 200])).wait_for_publish()
client.publish(topic, frame(3)).wait_for_publish()
time.sleep(1)
client.loop_stop()
client.disconnect()
print("PASS: published one-second EchoLink voice")
