import os

import pytest


@pytest.fixture(scope="session")
def _qemu_env():
    os.environ["QEMU_SYSTEM_XTENSA"] = os.path.expanduser(
        "~/.espressif/tools/qemu-xtensa/esp_develop_9.2.2_20260417/qemu/bin/qemu-system-xtensa"
    )
