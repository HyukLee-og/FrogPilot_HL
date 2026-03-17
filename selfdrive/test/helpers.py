import os

import cereal.messaging as messaging

from openpilot.common.params import Params
from openpilot.system.version import terms_version, training_version


def set_params_enabled() -> None:
  os.environ["FINGERPRINT"] = "TOYOTA_COROLLA_TSS2"
  os.environ["LOGPRINT"] = "debug"

  params = Params()
  params.put("HasAcceptedTerms", terms_version)
  params.put("CompletedTrainingVersion", training_version)
  params.put_bool("OpenpilotEnabledToggle", True)

  # Seed a valid calibration so simulator flows can start without setup UI.
  msg = messaging.new_message("liveCalibration")
  msg.liveCalibration.validBlocks = 20
  msg.liveCalibration.rpyCalib = [0.0, 0.0, 0.0]
  params.put("CalibrationParams", msg.to_bytes())
