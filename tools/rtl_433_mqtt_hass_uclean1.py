#!/usr/bin/env python3
# coding=utf-8
#
# Minimal Home Assistant MQTT discovery bridge for the Uponor Clean 1
# rtl_433 decoder (andy778/rtl_433, branch add-uponor-clean1-upstream).
# Trimmed from upstream's examples/rtl_433_mqtt_hass.py: only the field
# mappings this decoder actually emits (msg_name, msg_type, role,
# node_dst) are kept.
#
# Alarms are not a plain field mapping. The outer unit sends [type, state]
# for each of the five alarm types separately, and only when that type
# changes (FUN_e0e5; all five as clear at boot, FUN_e372). One shared
# msg_state entity would show whichever type was sent last, and an
# expire_after on it would turn a days-long alarm "unavailable" a few
# minutes after its only frame. So the bridge republishes each alarm
# frame's state to its own retained topic, <device topic>/alarm/<msg_name>,
# with one binary_sensor per type and no expire_after. Link health is a
# separate connectivity binary_sensor fed by every frame from the node
# (heartbeat ~62 s), which turns off -x seconds after the last one.

from __future__ import print_function
from __future__ import with_statement

AP_DESCRIPTION = "Publish Home Assistant MQTT discovery topics for the Uponor Clean 1 rtl_433 decoder."

import os
import argparse
import logging
import time
import json
import paho.mqtt.client as mqtt
import re


discovery_timeouts = {}

# Fields that get ignored when publishing to Home Assistant
# (reduces noise to help spot missing field mappings)
SKIP_KEYS = [ "type", "model", "subtype", "channel", "id", "mic", "mod",
                "freq", "sequence_num", "message_type", "exception", "raw_msg" ]


# Mapping of this decoder's own output fields to Home Assistant metadata.
mappings = {
    "msg_name": {
        "device_type": "sensor",
        "object_suffix": "alarm",
        "config": {
            "name": "Uponor Last Message",
            "icon": "mdi:message-outline",
            "entity_category": "diagnostic",
            "value_template": "{{ value }}"
        }
    },

    "msg_type": {
        "device_type": "sensor",
        "object_suffix": "msgtype",
        "config": {
            "name": "Uponor Message Type",
            "entity_category": "diagnostic",
            "enabled_by_default": False,
            "value_template": "{{ value }}"
        }
    },

    "role": {
        "device_type": "sensor",
        "object_suffix": "role",
        "config": {
            "name": "Uponor Frame Role",
            "entity_category": "diagnostic",
            "enabled_by_default": False,
            "value_template": "{{ value }}"
        }
    },

    "node_dst": {
        "device_type": "sensor",
        "object_suffix": "node_dst",
        "config": {
            "name": "Uponor Destination Node",
            "entity_category": "diagnostic",
            "enabled_by_default": False,
            "value_template": "{{ value }}"
        }
    },

}

# The five alarm types (msg_name as emitted by the decoder) and the display
# codes behind each. One retained binary_sensor per type; no icon override,
# so HA's device_class "problem" icon shows the state.
ALARM_TYPES = {
    "chemical_low":    ("Uponor Chemical Low",    "E021"),
    "high_water":      ("Uponor High Water",      "E031-E033"),
    "device_fault":    ("Uponor Device Fault",    "E040-E045, E047"),
    "sludge_reminder": ("Uponor Sludge Reminder", "E051"),
    "gsm_fault":       ("Uponor GSM Fault",       "E012"),
}

# Retained discovery config of the old single "Uponor Alarm Active" entity
# (object suffix "fault"); cleared once per device so it doesn't linger.
LEGACY_OBJECT_SUFFIXES = [("binary_sensor", "fault")]

LINK_TIMEOUT_DEFAULT = 150  # ~2.5 missed heartbeats when -x isn't given

cleared_legacy = set()


def alarm_mapping(name):
    """Discovery mapping for one alarm type's binary_sensor."""
    label, _codes = ALARM_TYPES[name]
    return {
        "device_type": "binary_sensor",
        "object_suffix": name,
        "no_expire": True,
        "config": {
            "name": label,
            "device_class": "problem",
            "payload_on": "1",
            "payload_off": "0",
        },
    }


def link_mapping(timeout):
    """Discovery mapping for the radio-link connectivity binary_sensor."""
    return {
        "device_type": "binary_sensor",
        "object_suffix": "link",
        "no_expire": True,
        "config": {
            "name": "Uponor Radio Link",
            "device_class": "connectivity",
            "entity_category": "diagnostic",
            "payload_on": "ON",
            "off_delay": timeout,
        },
    }


TOPIC_PARSE_RE = re.compile(r'\[(?P<slash>/?)(?P<token>[^\]:]+):?(?P<default>[^\]:]*)\]')

def mqtt_connect(client, userdata, flags, rc):
    """Callback for MQTT connects."""

    logging.info("MQTT connected: " + mqtt.connack_string(rc))
    if rc != 0:
        logging.error("Could not connect. Error: " + str(rc))
    else:
        logging.info("Subscribing to: " + args.rtl_topic)
        client.subscribe(args.rtl_topic)


def mqtt_disconnect(client, userdata, rc):
    """Callback for MQTT disconnects."""
    logging.info("MQTT disconnected: " + mqtt.connack_string(rc))


def mqtt_message(client, userdata, msg):
    """Callback for MQTT message PUBLISH."""
    logging.debug("MQTT message: " + json.dumps(msg.payload.decode()))

    try:
        # Decode JSON payload
        data = json.loads(msg.payload.decode())

    except json.decoder.JSONDecodeError:
        logging.error("JSON decode error: " + msg.payload.decode())
        return

    topicprefix = "/".join(msg.topic.split("/", 2)[:2])
    bridge_event_to_hass(client, topicprefix, data)


def sanitize(text):
    """Sanitize a name for Graphite/MQTT use."""
    return (text
            .replace(" ", "_")
            .replace("/", "_")
            .replace(".", "_")
            .replace("&", ""))

def rtl_433_device_info(data, topic_prefix):
    """Return rtl_433 device topic to subscribe to for a data element, based on the
    rtl_433 device topic argument, as well as the device identifier"""

    path_elements = []
    id_elements = []
    last_match_end = 0
    # The default for args.device_topic_suffix is the same topic structure
    # as set by default in rtl433 config
    for match in re.finditer(TOPIC_PARSE_RE, args.device_topic_suffix):
        path_elements.append(args.device_topic_suffix[last_match_end:match.start()])
        key = match.group(2)
        if key in data:
            # If we have this key, prepend a slash if needed
            if match.group(1):
                path_elements.append('/')
            element = sanitize(str(data[key]))
            path_elements.append(element)
            id_elements.append(element)
        elif match.group(3):
            path_elements.append(match.group(3))
        last_match_end = match.end()

    path = ''.join(list(filter(lambda item: item, path_elements)))
    id = '-'.join(id_elements)
    return (f"{topic_prefix}/{path}", id)


def publish_config(mqttc, topic, model, object_id, mapping, key=None):
    """Publish Home Assistant auto discovery data."""
    global discovery_timeouts

    device_type = mapping["device_type"]
    object_suffix = mapping["object_suffix"]
    object_name = "-".join([object_id, object_suffix])

    path = "/".join([args.discovery_prefix, device_type, object_id, object_name, "config"])

    # check timeout
    now = time.time()
    if path in discovery_timeouts:
        if discovery_timeouts[path] > now:
            logging.debug("Discovery timeout in the future for: " + path)
            return False

    discovery_timeouts[path] = now + args.discovery_interval

    config = mapping["config"].copy()

    # Device Automation configuration is in a different structure compared to
    # all other mqtt discovery types.
    # https://www.home-assistant.io/integrations/device_trigger.mqtt/
    if device_type == 'device_automation':
        config["topic"] = topic
        config["platform"] = 'mqtt'
    else:
        readable_name = mapping["config"]["name"] if "name" in mapping["config"] else key
        config["state_topic"] = topic
        config["unique_id"] = object_name
        config["name"] = readable_name

    try:
        hassio_device_mfgr, hassio_device_modl = model.split('-', 1)
        config["device"] = { "identifiers": [object_id], "name": object_id, "model": hassio_device_modl, "manufacturer": hassio_device_mfgr }
    except ValueError:
        config["device"] = { "identifiers": [object_id], "name": object_id, "model": model, "manufacturer": "rtl_433" }

    if args.force_update:
        config["force_update"] = "true"

    if args.expire_after and not mapping.get("no_expire"):
        config["expire_after"] = args.expire_after

    logging.debug(path + ":" + json.dumps(config))

    mqttc.publish(path, json.dumps(config), retain=args.retain)

    return True

def bridge_event_to_hass(mqttc, topic_prefix, data):
    """Translate some rtl_433 sensor data to Home Assistant auto discovery."""

    if "model" not in data:
        # not a device event
        logging.debug("Model is not defined. Not sending event to Home Assistant.")
        return

    model = sanitize(data["model"])

    skipped_keys = []
    published_keys = []

    base_topic, device_id = rtl_433_device_info(data, topic_prefix)
    if not device_id:
        # no unique device identifier
        logging.warning("No suitable identifier found for model: %s", model)
        return

    if args.ids and "id" in data and str(data.get("id")) not in args.ids:
        # not in the safe list
        logging.debug("Device (%s) is not in the desired list of device ids: [%s]" % (data["id"], ids))
        return

    # detect known attributes
    for key in data.keys():
        if key in mappings:
            # topic = "/".join([topicprefix,"devices",model,instance,key])
            topic = "/".join([base_topic, key])
            if publish_config(mqttc, topic, model, device_id, mappings[key], key):
                published_keys.append(key)
        else:
            if key not in SKIP_KEYS:
                skipped_keys.append(key)

    if published_keys:
        logging.info("Published %s: %s" % (device_id, ", ".join(published_keys)))

        if skipped_keys:
            logging.info("Skipped %s: %s" % (device_id, ", ".join(skipped_keys)))

    clear_legacy_entities(mqttc, device_id)
    bridge_link(mqttc, base_topic, model, device_id)
    bridge_alarm(mqttc, base_topic, model, device_id, data)


def clear_legacy_entities(mqttc, device_id):
    """Remove the retired single alarm entity's discovery config, once per device."""
    if device_id in cleared_legacy:
        return
    cleared_legacy.add(device_id)
    for device_type, suffix in LEGACY_OBJECT_SUFFIXES:
        object_name = "-".join([device_id, suffix])
        path = "/".join([args.discovery_prefix, device_type, device_id, object_name, "config"])
        mqttc.publish(path, "", retain=True)


def bridge_link(mqttc, base_topic, model, device_id):
    """Every frame from a node (heartbeat ~62 s, or ACK) marks its link up."""
    topic = base_topic + "/link"
    timeout = args.expire_after or LINK_TIMEOUT_DEFAULT
    publish_config(mqttc, topic, model, device_id, link_mapping(timeout))
    # Not retained: after an HA restart the link reads unknown until the next
    # frame rather than "connected" from a stale message.
    mqttc.publish(topic, "ON", retain=False)


def bridge_alarm(mqttc, base_topic, model, device_id, data):
    """Republish an alarm frame's state to its type's own retained topic."""
    if data.get("role") != "message":
        return
    name = data.get("msg_name")
    if name != "heartbeat" and name not in ALARM_TYPES:
        return

    # The node that sends heartbeats/alarms is the outer unit: announce all
    # five so they exist before the first alarm frame (they read "unknown"
    # until the unit's next change or boot burst).
    for alarm in ALARM_TYPES:
        publish_config(mqttc, "/".join([base_topic, "alarm", alarm]),
                       model, device_id, alarm_mapping(alarm), alarm)

    if name in ALARM_TYPES and "msg_state" in data:
        state = str(data["msg_state"])
        # Retained: the unit sends each change once (it resends all five only
        # after a range test, FUN_e427), so the broker has to hold the
        # current state across HA and bridge restarts.
        mqttc.publish("/".join([base_topic, "alarm", name]), state, retain=True)
        logging.info("Alarm %s %s: %s" % (device_id, name, state))


def rtl_433_bridge():
    """Run a MQTT Home Assistant auto discovery bridge for rtl_433."""

    if hasattr(mqtt, 'CallbackAPIVersion'):  # paho >= 2.0.0
        mqttc = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION1)
    else:
        mqttc = mqtt.Client()

    if args.debug:
        mqttc.enable_logger()

    if args.user is not None:
        mqttc.username_pw_set(args.user, args.password)

    if args.ca_cert is not None:
        mqttc.tls_set(certfile=args.cert, keyfile=args.key, ca_certs=args.ca_cert)

    mqttc.on_connect = mqtt_connect
    mqttc.on_disconnect = mqtt_disconnect
    mqttc.on_message = mqtt_message
    mqttc.connect_async(args.host, args.port, 60)
    logging.debug("MQTT Client: Starting Loop")
    mqttc.loop_start()

    while True:
        time.sleep(1)


def run():
    """Run main or daemon."""
    # with daemon.DaemonContext(files_preserve=[sock]):
    #  detach_process=True
    #  uid
    #  gid
    #  working_directory
    rtl_433_bridge()


if __name__ == "__main__":
    logging.basicConfig(format='[%(asctime)s] %(levelname)s:%(name)s:%(message)s',datefmt='%Y-%m-%dT%H:%M:%S%z')
    logging.getLogger().setLevel(logging.INFO)

    parser = argparse.ArgumentParser(description=AP_DESCRIPTION)

    parser.add_argument("-d", "--debug", action="store_true")
    parser.add_argument("-q", "--quiet", action="store_true")
    parser.add_argument("-u", "--user", type=str, help="MQTT username")
    parser.add_argument("-P", "--password", type=str, help="MQTT password")
    parser.add_argument("-H", "--host", type=str, default="127.0.0.1",
                        help="MQTT hostname to connect to (default: %(default)s)")
    parser.add_argument("-p", "--port", type=int, default=1883,
                        help="MQTT port (default: %(default)s)")
    parser.add_argument("-c", "--ca_cert", type=str, help="MQTT TLS CA certificate path")
    parser.add_argument("--cert", type=str, help="MQTT TLS certificate path")
    parser.add_argument("--key", type=str, help="MQTT TLS certificate key path")
    parser.add_argument("-r", "--retain", action="store_true")
    parser.add_argument("-f", "--force_update", action="store_true",
                        help="Append 'force_update = true' to all configs.")
    parser.add_argument("-R", "--rtl-topic", type=str,
                        default="rtl_433/+/events",
                        dest="rtl_topic",
                        help="rtl_433 MQTT event topic to subscribe to (default: %(default)s)")
    parser.add_argument("-D", "--discovery-prefix", type=str,
                        dest="discovery_prefix",
                        default="homeassistant",
                        help="Home Assistant MQTT topic prefix (default: %(default)s)")
    # This defaults to the rtl433 config default, so we assemble the same topic structure
    parser.add_argument("-T", "--device-topic_suffix", type=str,
                        dest="device_topic_suffix",
                        default="devices[/type][/model][/subtype][/channel][/id]",
                        help="rtl_433 device topic suffix (default: %(default)s)")
    parser.add_argument("-i", "--interval", type=int,
                        dest="discovery_interval",
                        default=600,
                        help="Interval to republish config topics in seconds (default: %(default)d)")
    parser.add_argument("-x", "--expire-after", type=int,
                        dest="expire_after",
                        help="Seconds with no frame from a node after which its Radio Link turns off "
                             "and its diagnostic sensors become unavailable (default for the link: "
                             "%d). Alarm sensors never expire." % LINK_TIMEOUT_DEFAULT)
    parser.add_argument("-I", "--ids", type=str, nargs="+",
                        help="ID's of devices that will be discovered (omit for all)")
    args = parser.parse_args()

    if args.debug and args.quiet:
        logging.critical("Debug and quiet can not be specified at the same time")
        exit(1)

    if args.debug:
        logging.info("Enabling debug logging")
        logging.getLogger().setLevel(logging.DEBUG)
    if args.quiet:
        logging.getLogger().setLevel(logging.ERROR)

    # allow setting MQTT username and password via environment variables
    if not args.user and 'MQTT_USERNAME' in os.environ:
        args.user = os.environ['MQTT_USERNAME']

    if not args.password and 'MQTT_PASSWORD' in os.environ:
        args.password = os.environ['MQTT_PASSWORD']

    if not args.user or not args.password:
        logging.warning("User or password is not set. Check credentials if subscriptions do not return messages.")

    if args.ids:
        ids = ', '.join(args.ids)
        logging.info("Only discovering devices with ids: [%s]" % ids)
    else:
        logging.info("Discovering all devices")

    run()
