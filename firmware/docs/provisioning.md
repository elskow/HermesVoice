# Provisioning (no secrets in git)

## Rule

`sdkconfig.defaults` holds dev fallbacks only. Real WiFi/MQTT credentials
are written to the NVS partition at manufacturing or first boot, never
committed.

## Option A: serial provision (simplest, small fleets)

```bash
# after flashing, use the IDF NVS partition tool or a one-time serial
# command that calls node_config_set_wifi() / node_config_set_mqtt_uri()
```

`node_config` prioritizes NVS over Kconfig automatically.

## Option B: captive portal (user-facing products)

Add a `node_portal` component later: on boot with no creds, start a
SoftAP + web form, write submitted values via `node_config_set_*`,
reboot into STA mode. Not included by default to keep the base small.

## Manufacturing checklist

1. Flash firmware + blank NVS.
2. Write per-device `device_id` (e.g. `node-<serial>`) via `node_config_set_device_id()`.
3. Write WiFi/MQTT creds per deployment.
4. Verify `node/<id>/status` = `online` on the broker.
