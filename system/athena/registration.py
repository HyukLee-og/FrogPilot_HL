#!/usr/bin/env python3
import time
import json
import jwt
import random
import string
from pathlib import Path

from datetime import datetime, timedelta
from openpilot.common.api import api_get
from openpilot.common.params import Params
from openpilot.common.spinner import Spinner
from openpilot.selfdrive.controls.lib.alertmanager import set_offroad_alert
from openpilot.system.hardware import HARDWARE, PC
from openpilot.system.hardware.hw import Paths
from openpilot.common.swaglog import cloudlog


UNREGISTERED_DONGLE_ID = "UnregisteredDevice"


def is_registered_device() -> bool:
  dongle = Params().get("DongleId", encoding='utf-8')
  return dongle not in (None, UNREGISTERED_DONGLE_ID)


def register(show_spinner=False) -> str | None:
  params = Params()

  IMEI = params.get("IMEI", encoding='utf8')
  HardwareSerial = params.get("HardwareSerial", encoding='utf8')
  dongle_id: str | None = params.get("DongleId", encoding='utf8')
  needs_registration = None in (IMEI, HardwareSerial, dongle_id)
  needs_registration |= dongle_id == UNREGISTERED_DONGLE_ID

  pubkey = Path(Paths.persist_root()+"/comma/id_rsa.pub")
  if not pubkey.is_file():
    dongle_id = UNREGISTERED_DONGLE_ID
    cloudlog.warning(f"missing public key: {pubkey}")
  elif needs_registration:
    if show_spinner:
      spinner = Spinner()
      spinner.update("registering device")

    # Create registration token, in the future, this key will make JWTs directly
    with open(Paths.persist_root()+"/comma/id_rsa.pub") as f1, open(Paths.persist_root()+"/comma/id_rsa") as f2:
      public_key = f1.read()
      private_key = f2.read()

    # Block until we get the imei
    serial = HARDWARE.get_serial()
    start_time = time.monotonic()
    imei1: str | None = None
    imei2: str | None = None
    while imei1 is None and imei2 is None:
      try:
        imei1, imei2 = HARDWARE.get_imei(0), HARDWARE.get_imei(1)
      except Exception:
        cloudlog.exception("Error getting imei, trying again...")
        time.sleep(1)

      if time.monotonic() - start_time > 5 and show_spinner:
        spinner.update(f"registering device - serial: {serial}, IMEI: ({imei1}, {imei2})")

    params.put("IMEI", imei1)
    params.put("HardwareSerial", serial)

    backoff = 0
    start_time = time.monotonic()
    while True:
      try:
    # Bypass registration by generating a random dongle_id
    cloudlog.warning("Bypassing registration with fake ID")
    dongle_id = ''.join(random.choices(string.ascii_lowercase + string.digits, k=16))
    params.put("DongleId", dongle_id)
    return dongle_id


if __name__ == "__main__":
  print(register())
