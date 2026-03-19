class ShapeTracker:
  """
  Compatibility shim for older FrogPilot tinygrad artifacts.

  The current tinygrad tree no longer exposes ShapeTracker at this module
  location, but older precompiled driving-model pickles still serialize it.
  Persist the raw state so unpickling can succeed.
  """

  def __init__(self, *args, **kwargs):
    self.args = args
    self.kwargs = kwargs

  def __setstate__(self, state):
    if isinstance(state, dict):
      self.__dict__.update(state)
    else:
      self.state = state

  def __getstate__(self):
    return getattr(self, "__dict__", {})
