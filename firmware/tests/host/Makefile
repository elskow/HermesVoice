CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -DNODE_HOST_TEST
INCLUDES = -I../../main \
           -I../../components/node_ota/include \
           -I../../components/node_ptt/include \
           -I../../components/node_config/include \
           -I../../components/node_provision/include \
           -I../../components/node_health/include \
           -I../../components/node_mqtt/include \
           -I../../components/node_wifi/include \
           -I../../components/node_audio/include \
           -I../../components/node_led/include \
           -I../../components/node_display/include \
           -I../../components/node_voice/include \
           -I../../components/node_log/include

# Host-test defines: stub out IDF (no silicon needed). Kconfig values
# do not exist on host, so provide the few CONFIG_* the sources read.
KCONFIG_FLAGS = -DCONFIG_NODE_DEVICE_ID='"node-test"' \
  -DCONFIG_NODE_WIFI_SSID='"ssid"' -DCONFIG_NODE_WIFI_PASS='""' \
  -DCONFIG_NODE_MQTT_URI='"mqtt://localhost:1883"' \
  -DCONFIG_NODE_GATEWAY_URL='"http://localhost:8081"' \
  -DCONFIG_NODE_TELEMETRY_INTERVAL_S=30 \
  -DCONFIG_NODE_SILENCE_PEAK=300 \
  -DCONFIG_NODE_WIFI_ENABLED=1

SRCS = test_main.c ../../main/app_main.c ../../main/app_test.c ../../main/page.c ../../main/errors.c \
  ../../components/node_ptt/sim/node_ptt_sim.c \
  ../../components/node_audio/sim/node_audio_sim.c \
  ../../components/node_led/sim/node_led_sim.c \
  ../../components/node_display/common/display_layout.c \
  ../../components/node_config/sim/node_config_sim.c \
  ../../components/node_provision/src/node_provision.c \
  ../../components/node_health/sim/node_health_sim.c \
  ../../components/node_mqtt/sim/node_mqtt_sim.c \
  ../../components/node_ota/sim/node_ota_sim.c \
  ../../components/node_wifi/sim/node_wifi_sim.c \
  ../../components/node_voice/sim/node_voice_sim.c

test_all: $(SRCS)
	$(CC) $(CFLAGS) $(KCONFIG_FLAGS) $(INCLUDES) -o $@ $(SRCS)

run: test_all
	./test_all

clean:
	rm -f test_all

.PHONY: run clean
