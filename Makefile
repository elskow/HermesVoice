# Monorepo root: firmware/ (ESP32) + relay/ (Go) + docs/ (contracts).
# Whole project operable from here, no cd needed.
#
# Daily loop (no silicon, no flash):
#   make sim-build   one-time per clean tree (or after esp32 build)
#   ./tools/dev.py   relay + linux sim e2e vs real Hermes, ~30s
#
# Silicon loop:
#   make firmware    ESP32 image in firmware/build/
#   make flash PORT=/dev/cu.usbserial-*
#
.PHONY: all firmware sim-build doctor relay run watch dev-relay check test test-fw test-relay test-linux test-qemu dev dev-sim flash clean

all: test

IDF ?= idf.py
IDF_PATH ?= $(HOME)/.espressif/v6.1/esp-idf
IDF_VENV ?= $(HOME)/.espressif/tools/python/v6.1/venv
IDF_TOOLS ?= $(HOME)/.espressif/tools

# Every idf.py recipe runs with this env on a bare host. No export.sh needed.
# Inside the espressif/idf container (IDF_PATH=/opt/esp/idf, export.sh already
# sourced) the container env is authoritative: WITH_IDF passes through so the
# exported venv (which owns rich_click and friends) is never shadowed by
# $HOME paths that do not exist in the container.
XTENSA ?= $(HOME)/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin
QEMU_BIN ?= $(HOME)/.espressif/tools/qemu-xtensa/esp_develop_9.2.2_20260417/qemu/bin
ifeq ($(IDF_PATH),/opt/esp/idf)
WITH_IDF = env
else
WITH_IDF = env "PATH=$(IDF_VENV)/bin:$(IDF_PATH)/tools:$(XTENSA):$(QEMU_BIN):/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin" "IDF_PATH=$(IDF_PATH)" "IDF_PYTHON_ENV_PATH=$(IDF_VENV)" "IDF_TOOLS_PATH=$(IDF_TOOLS)" "ESP_IDF_VERSION=6.1.0"
endif

# ESP32 image. Uses firmware/build/ only; never touches build-linux/.
# Strips CC/CXX so the linux sim's gcc-16 never leaks into the xtensa build.
# set-target runs once per fresh tree (like sim-build); steady state is build.
firmware:
	@if ! grep -q '^IDF_TARGET:STRING=esp32$$' firmware/build/CMakeCache.txt 2>/dev/null; then \
		echo "firmware: first configure for esp32"; \
		$(WITH_IDF) env -u CC -u CXX -u CFLAGS -u CXXFLAGS -u SDKCONFIG \
			$(IDF) -C firmware -B firmware/build set-target esp32; \
	fi
	$(WITH_IDF) env -u CC -u CXX -u CFLAGS -u CXXFLAGS -u SDKCONFIG \
		$(IDF) -C firmware -B firmware/build build

# Linux sim image. build-linux/ is self-contained: own CMakeCache target +
# own sdkconfig (-D SDKCONFIG absolute path). set-target runs once per tree
# (guard file); shared firmware/sdkconfig is backed up + restored around it.
# Needs Homebrew gcc on macOS (Apple clang 17 rejects IDF 6.1 mbedtls flags).
LINUX_SDKCONFIG = $(CURDIR)/firmware/build-linux/sdkconfig
LINUX_CONFIGURED = firmware/build-linux/.linux-configured
sim-build:
	@if [ "$$(uname)" = Darwin ] && ! command -v gcc-16 >/dev/null && ! [ -x /opt/homebrew/bin/gcc-16 ]; then \
		echo "need Homebrew gcc: brew install gcc"; exit 1; fi
	@if [ ! -f $(LINUX_CONFIGURED) ]; then \
		echo "sim-build: first configure for linux"; \
		rm -rf firmware/build-linux; \
		cp firmware/sdkconfig /tmp/sdkconfig.esp32.bak 2>/dev/null || true; \
		mkdir -p firmware/build-linux; \
		printf 'CONFIG_IDF_TARGET="linux"\n' > $(LINUX_SDKCONFIG); \
		SDKCONFIG=$(LINUX_SDKCONFIG) IDF_TARGET=linux \
		$(WITH_IDF) CC=gcc-16 CXX=g++-16 \
			$(IDF) -C firmware -B firmware/build-linux --preview set-target linux; \
		cp /tmp/sdkconfig.esp32.bak firmware/sdkconfig 2>/dev/null || true; \
		rm -f firmware/sdkconfig.old $(LINUX_SDKCONFIG).old; \
		touch $(LINUX_CONFIGURED); \
	fi
	$(WITH_IDF) CC=gcc-16 CXX=g++-16 CFLAGS="-DNODE_SIM_BUILD=1" IDF_TARGET=linux SDKCONFIG=$(LINUX_SDKCONFIG) $(IDF) -C firmware -B firmware/build-linux -D SDKCONFIG=$(LINUX_SDKCONFIG) build

# Relay dev intents: run (once, foreground), watch (rebuild on save),
# check (vet+test+virt parity). Old names kept as aliases.
run:
	cd relay && go run ./cmd/hermes-voice

relay: run

watch:
	cd relay && air

dev-relay: watch

check: test-relay
	@test -z "$$(gofmt -l relay/)" || (echo "gofmt dirty:"; gofmt -l relay/; exit 1)
	@RPORT=$$(python3 -c "import socket;s=socket.socket();s.bind(('127.0.0.1',0));print(s.getsockname()[1])"); \
	cd relay && PORT=$$RPORT STT_PROVIDER=stub HERMES_BASE_URL=http://127.0.0.1:9 \
	go run ./cmd/hermes-voice > /tmp/check-relay.log 2>&1 & echo $$! > /tmp/check-relay.pid; \
	sleep 6; cd relay && go run ./cmd/virt --scenario=cmd/virt/testdata/scenarios.txt,offline --relay=http://127.0.0.1:$$RPORT; \
	rc=$$?; kill `cat /tmp/check-relay.pid` 2>/dev/null; exit $$rc

test-relay:
	cd relay && go vet ./... && go test ./...

test: test-fw test-relay test-linux
	@test -z "$$(gofmt -l relay/)" || (echo "gofmt dirty:"; gofmt -l relay/; exit 1)

# QEMU e2e on the real xtensa binary (open_eth, no radio): boot, eth IP,
# heap-guard line, PTT init, ready, no panic. Slow (~3 min); not in `test`.
# Requires: qemu-system-xtensa on PATH.
QEMU_BUILD = $(CURDIR)/firmware/build-qemu
# test-qemu runs under the container venv (export.sh sourced by CI): IDF and
# python resolve from the exported env, never from $HOME-pinned Makefile vars.
test-qemu:
	SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" \
	$(WITH_IDF) idf.py -C firmware -B $(QEMU_BUILD) --preview set-target esp32
	SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" \
	$(WITH_IDF) idf.py -C firmware -B $(QEMU_BUILD) build
	$(WITH_IDF) python -m pytest firmware/tests/qemu \
		--embedded-services idf,qemu \
		--app-path $(CURDIR)/firmware --build-dir $(QEMU_BUILD) \
		--target esp32 -q

doctor:
	python3 ./tools/doctor.py

test-fw:
	make -C firmware/tests/host run

test-linux: sim-build
	./tools/dev.py

dev:
	./tools/dev.py

dev-sim:
	./tools/dev.py --sim-only --relay $(RELAY)

flash:
	$(WITH_IDF) $(IDF) -C firmware -B firmware/build -p $(PORT) flash monitor

clean:
	make -C firmware/tests/host clean
	rm -rf firmware/build firmware/build-linux firmware/build-qemu
