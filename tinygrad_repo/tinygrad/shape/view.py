class View:
  """
  Compatibility shim for older FrogPilot tinygrad artifacts.

  Newer tinygrad revisions removed the old tinygrad.shape.* classes from the
  import graph, but precompiled driving-model pickles still reference them by
  module path during unpickling. These objects are only needed to deserialize
  the captured graph metadata, so we store whatever state the pickle provides
  without trying to emulate the old movement API.
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
