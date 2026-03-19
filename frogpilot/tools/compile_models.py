#!/usr/bin/env python3
import os
import pickle
import requests
import shutil
import sys

from pathlib import Path

from openpilot.common.basedir import BASEDIR
from openpilot.common.params import Params

CANCEL_DOWNLOAD_PARAM = "CancelModelDownload"
DOWNLOAD_ALL_MODELS_PARAM = "DownloadAllModels"
DOWNLOAD_PROGRESS_PARAM = "ModelDownloadProgress"
MODEL_DOWNLOAD_PARAM = "ModelToDownload"
UPDATE_TINYGRAD_PARAM = "UpdateTinygrad"

MODELS_PATH = Path("/data/models")
METADATA_SCRIPT = Path(BASEDIR) / "selfdrive/modeld/get_model_metadata.py"
TINYGRAD_REPO_PATH = Path(BASEDIR) / "tinygrad_repo"
UNCOMPILED_DIR = Path(MODELS_PATH) / "uncompiled_downloads"
MODEL_COMPONENTS = ("driving_policy", "driving_vision")
COMPILED_MODEL_SUFFIXES = (
  "driving_policy_metadata.pkl",
  "driving_policy_tinygrad.pkl",
  "driving_vision_metadata.pkl",
  "driving_vision_tinygrad.pkl",
)
UNCOMPILED_MODEL_SUFFIXES = tuple(f"{component}.onnx" for component in MODEL_COMPONENTS)
COMPILED_MODEL_SOURCES = (
  "https://raw.githubusercontent.com/FrogAi/FrogPilot-Resources/Models/compiled",
  "https://gitlab.com/FrogAi/FrogPilot-Resources/-/raw/Models/compiled",
)
UNCOMPILED_MODEL_SOURCES = (
  "https://raw.githubusercontent.com/FrogAi/FrogPilot-Resources/Models/uncompiled",
  "https://gitlab.com/FrogAi/FrogPilot-Resources/-/raw/Models/uncompiled",
)
GITHUB_COMPILED_MODEL_INDEX_URL = "https://api.github.com/repos/FrogAi/FrogPilot-Resources/contents/compiled?ref=Models"
GITLAB_COMPILED_MODEL_INDEX_URL = "https://gitlab.com/api/v4/projects/FrogAi%2FFrogPilot-Resources/repository/tree?ref=Models&path=compiled&per_page=100"
GITHUB_UNCOMPILED_MODEL_INDEX_URL = "https://api.github.com/repos/FrogAi/FrogPilot-Resources/contents/uncompiled?ref=Models"
GITLAB_UNCOMPILED_MODEL_INDEX_URL = "https://gitlab.com/api/v4/projects/FrogAi%2FFrogPilot-Resources/repository/tree?ref=Models&path=uncompiled&per_page=100"
DOWNLOAD_CHUNK_SIZE = 16384


def decode_param(value) -> str:
  if isinstance(value, bytes):
    return value.decode("utf-8", errors="ignore")
  if value is None:
    return ""
  return str(value)


def delete_path(path: Path) -> None:
  if path.is_symlink() or path.is_file():
    path.unlink(missing_ok=True)
  elif path.is_dir():
    shutil.rmtree(path, ignore_errors=True)


def clear_download_requests(params_memory: Params) -> None:
  params_memory.remove(MODEL_DOWNLOAD_PARAM)
  params_memory.put_bool(DOWNLOAD_ALL_MODELS_PARAM, False)
  params_memory.put_bool(UPDATE_TINYGRAD_PARAM, False)
  params_memory.put_bool(CANCEL_DOWNLOAD_PARAM, False)


def get_available_model_keys(params: Params) -> list[str]:
  available = decode_param(params.get("AvailableModels"))
  return [model for model in available.split(",") if model and not model.endswith("_default")]


def has_all_model_files(model_key: str) -> bool:
  return all((Path(MODELS_PATH) / f"{model_key}_{suffix}").exists() for suffix in COMPILED_MODEL_SUFFIXES)


def get_model_key_from_filename(filename: str, suffixes: tuple[str, ...]) -> str | None:
  for suffix in suffixes:
    filename_suffix = f"_{suffix}"
    if filename.endswith(filename_suffix):
      return filename[:-len(filename_suffix)]
  return None


def fetch_remote_model_keys_from_indexes(session: requests.Session, github_url: str, gitlab_url: str,
                                         suffixes: tuple[str, ...], label: str) -> set[str]:
  remote_model_keys: set[str] = set()

  try:
    response = session.get(github_url, timeout=20)
    response.raise_for_status()
    for entry in response.json():
      name = entry.get("name", "")
      model_key = get_model_key_from_filename(name, suffixes)
      if model_key:
        remote_model_keys.add(model_key)
  except Exception as exception:
    print(f"GitHub {label} model index unavailable: {exception}")

  if remote_model_keys:
    return remote_model_keys

  try:
    response = session.get(gitlab_url, timeout=20)
    response.raise_for_status()
    for entry in response.json():
      name = entry.get("name", "")
      model_key = get_model_key_from_filename(name, suffixes)
      if model_key:
        remote_model_keys.add(model_key)
  except Exception as exception:
    print(f"GitLab {label} model index unavailable: {exception}")

  return remote_model_keys


def fetch_compiled_model_keys(session: requests.Session) -> set[str]:
  return fetch_remote_model_keys_from_indexes(
    session,
    GITHUB_COMPILED_MODEL_INDEX_URL,
    GITLAB_COMPILED_MODEL_INDEX_URL,
    COMPILED_MODEL_SUFFIXES,
    "compiled",
  )


def fetch_remote_model_keys(session: requests.Session) -> set[str]:
  compiled_model_keys = fetch_compiled_model_keys(session)
  if compiled_model_keys:
    return compiled_model_keys

  return fetch_remote_model_keys_from_indexes(
    session,
    GITHUB_UNCOMPILED_MODEL_INDEX_URL,
    GITLAB_UNCOMPILED_MODEL_INDEX_URL,
    UNCOMPILED_MODEL_SUFFIXES,
    "uncompiled",
  )


def get_requested_model_keys(params: Params, params_memory: Params, remote_model_keys: set[str] | None = None) -> list[str]:
  remote_model_keys = remote_model_keys or set()
  requested_model = decode_param(params_memory.get(MODEL_DOWNLOAD_PARAM))
  if requested_model:
    if remote_model_keys and requested_model not in remote_model_keys:
      return []
    return [requested_model]

  if params_memory.get_bool(DOWNLOAD_ALL_MODELS_PARAM):
    return [
      model_key for model_key in get_available_model_keys(params)
      if (not remote_model_keys or model_key in remote_model_keys) and not has_all_model_files(model_key)
    ]

  return []


def run_command(cmd: list[str], env: dict[str, str] | None = None) -> bool:
  try:
    subprocess_result = __import__("subprocess").run(cmd, capture_output=True, check=True, env=env, text=True)
    if subprocess_result.stdout.strip():
      print(subprocess_result.stdout.strip())
    return True
  except Exception as exception:
    print(f"Command failed: {' '.join(cmd)}")
    print(exception)
    return False


def get_compile_env() -> dict[str, str]:
  env = os.environ.copy()
  env["PYTHONPATH"] = f"{env.get('PYTHONPATH', '')}:{TINYGRAD_REPO_PATH}"

  # Match the same tinygrad compile flags used by the built-in model build so
  # downloaded models don't produce a larger, slower generic artifact on device.
  if os.path.exists("/TICI"):
    env["DEV"] = "QCOM"
    env["FLOAT16"] = "1"
    env["NOLOCALS"] = "1"
    env["IMAGE"] = "2"
    env["JIT_BATCH_SIZE"] = "0"
  else:
    env.setdefault("DEV", "CPU")
    env.setdefault("CPU_LLVM", "1")

  return env


def ensure_tinygrad_pickle_compat() -> None:
  from tinygrad.uop import Ops
  from tinygrad.renderer import ProgramSpec, Estimates

  if not hasattr(Ops, "VIEW") and hasattr(Ops, "BUFFER_VIEW"):
    Ops.VIEW = Ops.BUFFER_VIEW
  if not hasattr(Ops, "RECIP") and hasattr(Ops, "RECIPROCAL"):
    Ops.RECIP = Ops.RECIPROCAL
  if not hasattr(Ops, "ENDRANGE") and hasattr(Ops, "END"):
    Ops.ENDRANGE = Ops.END

  if getattr(ProgramSpec, "_frogpilot_safe_estimates", False):
    return

  def safe_estimates(self) -> Estimates:
    if self.uops is None:
      return Estimates()

    try:
      return Estimates.from_uops(self.uops, ignore_indexing=True)
    except Exception:
      return Estimates()

  ProgramSpec.estimates = property(safe_estimates)
  ProgramSpec._frogpilot_safe_estimates = True


def verify_download(session: requests.Session, file_path: Path, url: str) -> bool:
  if not file_path.exists() or file_path.stat().st_size == 0:
    return False

  try:
    response = session.head(url, headers={"Accept-Encoding": "identity"}, timeout=10)
    response.raise_for_status()
    remote_size = int(response.headers.get("Content-Length", 0))
    return remote_size == 0 or remote_size == file_path.stat().st_size
  except Exception:
    return True


def download_file(session: requests.Session, destination: Path, filename: str, params_memory: Params,
                  sources: tuple[str, ...]) -> Path | None:
  temp_path = destination.with_suffix(destination.suffix + ".tmp")
  delete_path(temp_path)

  for base_url in sources:
    url = f"{base_url}/{filename}"
    try:
      with session.get(url, stream=True, timeout=20) as response:
        if response.status_code == 404:
          continue

        response.raise_for_status()
        total_size = int(response.headers.get("Content-Length", 0))
        downloaded_size = 0

        with temp_path.open("wb") as temp_file:
          for chunk in response.iter_content(chunk_size=DOWNLOAD_CHUNK_SIZE):
            if params_memory.get_bool(CANCEL_DOWNLOAD_PARAM):
              delete_path(temp_path)
              params_memory.put(DOWNLOAD_PROGRESS_PARAM, "Download cancelled...")
              return None

            if not chunk:
              continue

            temp_file.write(chunk)
            downloaded_size += len(chunk)

            if total_size > 0:
              progress = min(99, round(downloaded_size / total_size * 100))
              params_memory.put(DOWNLOAD_PROGRESS_PARAM, f"{filename} {progress}%")
            else:
              params_memory.put(DOWNLOAD_PROGRESS_PARAM, f"Downloading {filename}...")

      temp_path.replace(destination)

      if verify_download(session, destination, url):
        return destination

      delete_path(destination)
    except Exception as exception:
      print(f"Failed downloading {filename} from {url}: {exception}")
      delete_path(temp_path)

  params_memory.put(DOWNLOAD_PROGRESS_PARAM, f"Failed: Missing {filename}")
  return None


def compile_model(onnx_path: Path, params_memory: Params) -> bool:
  compiled_path = Path(MODELS_PATH) / f"{onnx_path.stem}_tinygrad.pkl"
  metadata_temp_path = onnx_path.parent / f"{onnx_path.stem}_metadata.pkl"
  metadata_path = Path(MODELS_PATH) / metadata_temp_path.name

  delete_path(compiled_path)
  delete_path(metadata_temp_path)
  delete_path(metadata_path)

  env = get_compile_env()

  params_memory.put(DOWNLOAD_PROGRESS_PARAM, f"Compiling {onnx_path.stem}...")
  compiled = run_command([
    sys.executable,
    str(TINYGRAD_REPO_PATH / "examples/openpilot/compile3.py"),
    str(onnx_path),
    str(compiled_path),
  ], env=env)
  if not compiled:
    return False

  params_memory.put(DOWNLOAD_PROGRESS_PARAM, f"Extracting metadata for {onnx_path.stem}...")
  extracted = run_command([sys.executable, str(METADATA_SCRIPT), str(onnx_path)])
  if not extracted:
    return False

  if not metadata_temp_path.exists():
    print(f"Metadata file missing after extraction: {metadata_temp_path}")
    return False

  metadata_temp_path.replace(metadata_path)
  delete_path(onnx_path)
  return compiled_path.exists() and metadata_path.exists()


def download_compiled_model(session: requests.Session, model_key: str, params_memory: Params) -> bool:
  downloaded_paths: list[Path] = []

  for suffix in COMPILED_MODEL_SUFFIXES:
    filename = f"{model_key}_{suffix}"
    destination = Path(MODELS_PATH) / filename
    delete_path(destination)

    downloaded_path = download_file(session, destination, filename, params_memory, COMPILED_MODEL_SOURCES)
    if downloaded_path is None:
      for path in downloaded_paths:
        delete_path(path)
      return False

    downloaded_paths.append(downloaded_path)

  if not validate_compiled_model(downloaded_paths):
    print(f"Downloaded compiled artifacts for {model_key} are incompatible with current tinygrad runtime.")
    for path in downloaded_paths:
      delete_path(path)
    return False

  return True


def download_uncompiled_model_component(session: requests.Session, model_key: str, component: str,
                                        params_memory: Params) -> Path | None:
  filename = f"{model_key}_{component}.onnx"
  destination = UNCOMPILED_DIR / filename
  delete_path(destination)

  return download_file(session, destination, filename, params_memory, UNCOMPILED_MODEL_SOURCES)


def validate_compiled_model(paths: list[Path]) -> bool:
  try:
    ensure_tinygrad_pickle_compat()

    for path in paths:
      if path.name.endswith("_tinygrad.pkl"):
        with path.open("rb") as f:
          pickle.load(f)
    return True
  except Exception as exception:
    print(f"Compiled model validation failed: {exception}")
    return False


def prune_downloaded_models(params: Params, params_memory: Params) -> None:
  params_memory.put(DOWNLOAD_PROGRESS_PARAM, "Updating...")

  for pattern in ("*_driving_policy_metadata.pkl", "*_driving_policy_tinygrad.pkl",
                  "*_driving_vision_metadata.pkl", "*_driving_vision_tinygrad.pkl"):
    for path in Path(MODELS_PATH).glob(pattern):
      delete_path(path)

  params.put_bool("TinygradUpdateAvailable", False)
  params_memory.put(DOWNLOAD_PROGRESS_PARAM, "Updated!")


def process_model_download_request(params: Params | None = None, params_memory: Params | None = None) -> bool:
  params = params or Params()
  params_memory = params_memory or Params(memory=True)

  update_requested = params_memory.get_bool(UPDATE_TINYGRAD_PARAM)
  download_all = params_memory.get_bool(DOWNLOAD_ALL_MODELS_PARAM)

  if not update_requested and not download_all and not decode_param(params_memory.get(MODEL_DOWNLOAD_PARAM)):
    return False

  try:
    Path(MODELS_PATH).mkdir(parents=True, exist_ok=True)
    UNCOMPILED_DIR.mkdir(parents=True, exist_ok=True)

    if update_requested:
      prune_downloaded_models(params, params_memory)
      if not requested_model_keys:
        clear_download_requests(params_memory)
        return True

    session = requests.Session()
    session.headers.update({
      "Accept-Language": "en",
      "User-Agent": "frogpilot-model-manager/1.0 (https://github.com/FrogAi/FrogPilot)"
    })

    remote_model_keys = fetch_remote_model_keys(session)
    requested_model_keys = get_requested_model_keys(params, params_memory, remote_model_keys)

    if download_all and not requested_model_keys:
      params_memory.put(DOWNLOAD_PROGRESS_PARAM, "All models downloaded!")
      clear_download_requests(params_memory)
      return True

    if decode_param(params_memory.get(MODEL_DOWNLOAD_PARAM)) and not requested_model_keys:
      params_memory.put(DOWNLOAD_PROGRESS_PARAM, "Failed: Model unavailable")
      clear_download_requests(params_memory)
      return False

    for model_key in requested_model_keys:
      if params_memory.get_bool(CANCEL_DOWNLOAD_PARAM):
        params_memory.put(DOWNLOAD_PROGRESS_PARAM, "Download cancelled...")
        clear_download_requests(params_memory)
        return False

      if download_compiled_model(session, model_key, params_memory):
        continue

      print(f"Compiled artifacts unavailable for {model_key}, falling back to local compilation.")
      for component in MODEL_COMPONENTS:
        onnx_path = download_uncompiled_model_component(session, model_key, component, params_memory)
        if onnx_path is None:
          clear_download_requests(params_memory)
          return False

        if not compile_model(onnx_path, params_memory):
          params_memory.put(DOWNLOAD_PROGRESS_PARAM, "Download failed...")
          clear_download_requests(params_memory)
          return False

    params_memory.put(DOWNLOAD_PROGRESS_PARAM, "All models downloaded!" if download_all else "Downloaded!")
    clear_download_requests(params_memory)
    return True

  except Exception as exception:
    print(f"Model manager failed: {exception}")
    params_memory.put(DOWNLOAD_PROGRESS_PARAM, "Download failed...")
    clear_download_requests(params_memory)
    return False
  finally:
    delete_path(UNCOMPILED_DIR)


def main():
  process_model_download_request()


if __name__ == "__main__":
  main()
