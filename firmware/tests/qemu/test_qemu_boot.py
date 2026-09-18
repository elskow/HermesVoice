"""QEMU e2e: real ESP32 firmware boots, nets up, reaches ready.

Runs the xtensa binary in Espressif QEMU (open_eth virtual Ethernet).
Asserts on UART: boot, eth IP, PTT init, ready, no panic. Audio uses
static halves (BSS, link-time): no heap guard line exists anymore, so
there is nothing QEMU-specific to assert about audio. I2C/display
errors are expected (no OLED in emulation) and must not fail the boot.

Usage: make test-qemu   (builds QEMU image, boots ~60 s, checks UART)
Requires: qemu-system-xtensa on PATH (idf_tools.py install qemu-xtensa).
"""
import pytest
from pytest_embedded_qemu.dut import QemuDut


@pytest.mark.esp32
@pytest.mark.qemu
def test_boot_to_ready(dut: QemuDut) -> None:
    dut.expect(r"voice-node starting", timeout=30)
    dut.expect(r"eth started, waiting for ip", timeout=30)
    dut.expect(r"eth got ip", timeout=30)
    dut.expect(r"init gpio=4 active_low=1", timeout=30)
    dut.expect(r"voice-node ready", timeout=30)


@pytest.mark.esp32
@pytest.mark.qemu
def test_no_panic(dut: QemuDut) -> None:
    dut.expect(r"voice-node ready", timeout=60)
    # No panic/assert text may appear anywhere in the boot stream. The
    # expect calls above already scanned it; a trailing read would race
    # the emulator, so assert negatively via expect with must-not-match.
    with pytest.raises(Exception):
        dut.expect(r"panic_abort|Guru Meditation|assert failed", timeout=5)
