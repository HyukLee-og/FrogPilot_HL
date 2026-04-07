#!/usr/bin/env python3
import time

import cereal.messaging as messaging
from opendbc.can.parser import CANParser

from openpilot.common.params import Params


GM_MAKES = {"Buick", "Cadillac", "Chevrolet", "Gmc", "Holden"}
WATCH_BUSES = (0, 1, 2, 128, 130)
POLL_TIMEOUT_MS = 250
WAKE_COOLDOWN_SEC = 10.0


def build_parser(dbc_name: str, messages: list[str], bus: int) -> CANParser:
  return CANParser(dbc_name, [(msg, float("nan")) for msg in messages], bus)


def is_gm_make(params: Params) -> bool:
  return str(params.get("CarMake", return_default=True) or "") in GM_MAKES


def ignition_on(panda_states) -> bool:
  return any(state.ignitionLine or state.ignitionCan for state in panda_states)


def parser_active(parsers: list[CANParser], parser_input) -> bool:
  active = False
  for cp in parsers:
    cp.update(parser_input)

    if "Door_Open_Switch_Status_LS" in cp.vl:
      door_vals = cp.vl["Door_Open_Switch_Status_LS"]
      active |= bool(door_vals.get("DrDoorOpenSwAct", 0))
      active |= bool(door_vals.get("PsDoorOpenSwAct", 0))

    if "Door_Handle_Switch_Status_LS" in cp.vl:
      handle_vals = cp.vl["Door_Handle_Switch_Status_LS"]
      active |= any(bool(handle_vals.get(signal, 0)) for signal in (
        "DrvDrHndleSwAtv",
        "PasDrHndleSwAtv",
        "RLDrHndleSwAtv",
        "RRDrHndleSwAtv",
        "RCHndleSwAtv",
      ))

    if "DriverDoorStatus" in cp.vl:
      active |= bool(cp.vl["DriverDoorStatus"].get("DriverDoorOpened", 0))

  return active


def main() -> None:
  params = Params()
  params_memory = Params(memory=True)

  modern_parsers = [
    build_parser("gm_global_a_lowspeed_1818125", ["Door_Open_Switch_Status_LS", "Door_Handle_Switch_Status_LS"], bus)
    for bus in WATCH_BUSES
  ]
  legacy_parsers = [
    build_parser("gm_global_a_lowspeed", ["DriverDoorStatus"], bus)
    for bus in WATCH_BUSES
  ]

  sm = messaging.SubMaster(["deviceState", "pandaStates", "can"])

  last_active = False
  last_trigger_time = 0.0
  last_counter = int(params_memory.get("OffroadWakeCounter", return_default=True) or 0)

  while True:
    sm.update(POLL_TIMEOUT_MS)

    if not is_gm_make(params):
      last_active = False
      continue

    started = bool(sm["deviceState"].started)
    panda_states = sm["pandaStates"] if sm.updated["pandaStates"] else sm["pandaStates"]
    if started or ignition_on(panda_states):
      last_active = False
      continue

    can_msgs = sm["can"] if sm.updated["can"] else []
    if not can_msgs:
      continue

    parser_frames = [(int(msg.address), bytes(msg.dat), int(msg.src)) for msg in can_msgs]
    parser_input = [(time.monotonic_ns(), parser_frames)]
    active = parser_active(modern_parsers, parser_input) or parser_active(legacy_parsers, parser_input)

    now = time.monotonic()
    if active and not last_active and (now - last_trigger_time) >= WAKE_COOLDOWN_SEC:
      last_counter += 1
      params_memory.put_nonblocking("OffroadWakeCounter", last_counter)
      last_trigger_time = now

    last_active = active


if __name__ == "__main__":
  main()
