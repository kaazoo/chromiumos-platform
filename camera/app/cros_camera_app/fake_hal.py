# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module encapsulates various tasks for Fake HAL setup."""

import json
import logging
import pathlib
import re
import shutil
import subprocess
from typing import Any, Callable, Dict, List, Optional, TextIO, Tuple

from cros_camera_app import device


_PERSISTENT_CONFIG_PATH = pathlib.Path("/etc/camera/fake_hal.json")
_CONFIG_PATH = pathlib.Path("/run/camera/fake_hal.json")

_RESOLUTIONS = [
    (320, 180),
    (320, 240),
    (640, 360),
    (640, 480),
    (1280, 720),
    (1280, 960),
    (1600, 1200),
    (1920, 1080),
    (2560, 1440),
    (2592, 1944),
    (3264, 2448),
    (3840, 2160),
]

_FPS_RANGES = [(15, 30), (30, 30), (15, 60), (60, 60)]

_EMPTY_CONFIG = {"cameras": []}

# The implicit default formats setup by ParseCameraSpecs() in Fake HAL.
_IMPLICIT_FORMATS = [
    {"width": 320, "height": 240, "frame_rates": [[15, 60], [60, 60]]},
    {"width": 640, "height": 360, "frame_rates": [[15, 60], [60, 60]]},
    {"width": 640, "height": 480, "frame_rates": [[15, 60], [60, 60]]},
    {"width": 1280, "height": 720, "frame_rates": [[15, 60], [60, 60]]},
    {"width": 1920, "height": 1080, "frame_rates": [[15, 60], [60, 60]]},
]

# Workaround http://b/249339147 with some minimal sensible config, and make the
# editing style consistent with indents generated by json.dump().
_VIM_CONFIG = """
set nocompatible
set backspace=indent,eol,start

set laststatus=2
set number
set ruler

set autoindent
set tabstop=2
set softtabstop=2
set shiftwidth=2
"""

# The space character class here follows the JSON spec at
# https://www.rfc-editor.org/rfc/rfc8259#section-2. Note that \s in regular
# expression matches some extra characters like \f.
_TRAILING_COMMA = r"""(?x:
,           # comma itself
[ \t\r\n]*  # optional extra spaces
(]|})       # closing square/curly bracket as a capture group
)"""


def _load_json(s: str) -> Any:
    """Deserializes a Chromium base::Value flavored JSON string.

    Args:
        s: The JSON string to be deserialized.

    Returns:
        Deserialized value.
    """
    # Strip trailing commas, which are not supoprted by Python standard
    # library but might be used in Fake HAL.
    s = re.sub(_TRAILING_COMMA, r"\1", s)
    return json.loads(s)


def _load_config(path: pathlib.Path = _CONFIG_PATH) -> Dict:
    """Loads the config from JSON file.

    Args:
        path: Where to load config from.

    Returns:
        The deserialized config. If the config file does not exists, returns a
        valid empty config.
    """
    if not path.exists():
        return _EMPTY_CONFIG

    with open(path, encoding="utf-8") as f:
        s = f.read()
        return _load_json(s)


def _save_config(config: Dict):
    """Saves the config to the JSON file.

    Args:
        config: The config to be saved.
    """

    # Serialize the config to string first to catch any potential error, so it
    # won't break the config file with a partially written JSON.
    config_json = json.dumps(config, indent=2)

    # TODO(shik): Save config in a human-readable but concise JSON. Currently
    # the excessive line breaks for frame_rates field make it less pleasant.
    with open(_CONFIG_PATH, "w", encoding="utf-8") as f:
        f.write(config_json)


def _dump_config_info(path: pathlib.Path, dst: TextIO):
    """Dumps information of the config at given path.

    Args:
        path: The path of config file to dump information from.
        dst: The destination for writing the information.
    """

    def dump(indent: int, *values: Any):
        dst.write(" " * indent)
        print(*values, file=dst)

    if not path.exists():
        dump(0, "File not found.")
        return

    config = _load_config(path)

    for c in config["cameras"]:
        dump(2, "camera %d:" % c["id"])
        dump(4, "connected:", c["connected"])

        frame_desc = c.get("frames", {}).get("path", "Test Pattern")
        dump(4, "frame:", frame_desc)

        supported_formats = c.get("supported_formats", _IMPLICIT_FORMATS)
        if supported_formats is _IMPLICIT_FORMATS:
            dump(4, "formats: (implicit set by Fake HAL)")
        else:
            dump(4, "formats:")

        for f in supported_formats:
            fps = ", ".join([str(f) for f in f["frame_rates"]])
            dump(6, "%4dx%4d @ %s" % (f["width"], f["height"], fps))


# (width, height, fps_range) -> keep_or_not
FormatFilter = Callable[[int, int, Tuple[int, int]], bool]

# (camera_id) -> bool
CameraFilter = Callable[[int], bool]


def _generate_supported_formats(should_keep: FormatFilter) -> List[Dict]:
    """Generates and filters supported formats.

    Args:
        should_keep: A predicate callback to select the supported formats.

    Returns:
        A list of supported formats that match the filter.
    """
    formats = []
    for w, h in _RESOLUTIONS:
        frame_rates = []
        for fps in _FPS_RANGES:
            if should_keep(w, h, fps):
                # Convert tuple to list to be more consistent with JSON.
                frame_rates.append(list(fps))

        if frame_rates:
            fmt = {
                "width": w,
                "height": h,
                "frame_rates": frame_rates,
            }
            formats.append(fmt)
    return formats


def _get_next_available_id(config: Dict) -> int:
    """Gets an available camera id from config.

    Args:
        config: The Fake HAL config.

    Returns:
        An unused valid camera id.
    """
    used_ids = set(c["id"] for c in config.get("cameras", []))
    next_id = 1
    while next_id in used_ids:
        next_id += 1
    return next_id


def persist():
    """Persists the config file for Fake HAL."""

    if _CONFIG_PATH.exists():
        logging.info(
            "Copy config from %s to %s", _CONFIG_PATH, _PERSISTENT_CONFIG_PATH
        )
        shutil.copy2(_CONFIG_PATH, _PERSISTENT_CONFIG_PATH)
    elif _PERSISTENT_CONFIG_PATH.exists():
        logging.info(
            "Remove persistent Fake HAL config %s", _PERSISTENT_CONFIG_PATH
        )
        _PERSISTENT_CONFIG_PATH.unlink()
    else:
        logging.info("No config found, nothing to persist.")


def add_camera(
    *,
    should_keep: FormatFilter,
    frame_path: Optional[pathlib.Path] = None,
):
    """Adds a new camera.

    Args:
        should_keep: A predicate callback to select the supported formats.
        frame_path: The source of camera frame in jpg, mjpg, or y4m format. If
            not specified, a test pattern would be used.
    """
    config = _load_config()
    camera_id = _get_next_available_id(config)

    new_camera = {
        "id": camera_id,
        "connected": True,
        "supported_formats": _generate_supported_formats(should_keep),
    }
    if frame_path is not None:
        # Perform some smoke checks for the frame_path. Even if it failed, we
        # just print warnings and still write the config accordingly. Those
        # issues can be resolved later and the user may want to setup the
        # config first.
        if frame_path.exists():
            if not device.is_readable_by_camera_service(frame_path):
                logging.warning(
                    "%s might not be readable by camera service in minijail,"
                    " consider copying it to /var/cache/camera.",
                    frame_path,
                )
        else:
            logging.warning("%s does not exist", frame_path)

        # Convert to absolute path and plain string to be JSON friendly.
        path = frame_path.absolute().as_posix()
        new_camera["frames"] = {"path": path}
    logging.debug("new_camera = %s", new_camera)
    config["cameras"].append(new_camera)

    connected = [c["id"] for c in config["cameras"] if c["connected"]]

    logging.info("Added camera with id = %d", camera_id)
    logging.info(
        "%d cameras in config with %d connected: %s",
        len(config["cameras"]),
        len(connected),
        connected,
    )

    _save_config(config)


def remove_cameras(should_remove: CameraFilter):
    """Removes specified cameras.

    Args:
        should_remove: A predicate callback to select cameras to remove.
    """
    config = _load_config()

    kept_cameras = []
    removed_cameras = []
    for c in config["cameras"]:
        if should_remove(c["id"]):
            removed_cameras.append(c)
        else:
            kept_cameras.append(c)
    logging.info(
        "Removed %d camera(s): %s",
        len(removed_cameras),
        [c["id"] for c in removed_cameras],
    )
    logging.info(
        "Kept %d camera(s): %s",
        len(kept_cameras),
        [c["id"] for c in kept_cameras],
    )

    config["cameras"] = kept_cameras
    _save_config(config)


def edit_config_with_editor(editor: Optional[str]):
    """Edits the config with the provided editor.

    Args:
        editor: The editor to edit the config. If it's None, vim with a minimal
        vimrc would be used to make the editing style consistent with indents
        generated by json.dump().
    """
    if editor is None:
        cmd = ["vim", "-c", _VIM_CONFIG]
    else:
        cmd = [editor]
    cmd.extend(["--", str(_CONFIG_PATH)])
    subprocess.check_call(cmd)

    _load_config()
    logging.info("Config updated")
    # TODO(shik): Validate the config schema.


def connect_cameras(should_connect: CameraFilter):
    """Connects existing cameras.

    Args:
        should_connect: A predicate callback to select cameras to connect.
    """
    config = _load_config()
    for cam in config["cameras"]:
        if not cam["connected"] and should_connect(cam["id"]):
            cam["connected"] = True
            logging.info("Set connected to true for camera %d", cam["id"])
    _save_config(config)


def disconnect_cameras(should_disconnect: CameraFilter):
    """Connects existing cameras.

    Args:
        should_disconnect: A predicate callback to select cameras to disconnect.
    """
    config = _load_config()
    for cam in config["cameras"]:
        if cam["connected"] and should_disconnect(cam["id"]):
            cam["connected"] = False
            logging.info("Set connected to false for camera %d", cam["id"])
    _save_config(config)


def dump_config_info(dst: TextIO):
    """Dumps information of config files.

    Args:
        dst: The destination for writing the information.
    """
    print("Config %s:" % _CONFIG_PATH, file=dst)
    _dump_config_info(_CONFIG_PATH, dst)

    print("", file=dst)

    print("Persistent config %s:" % _PERSISTENT_CONFIG_PATH, file=dst)
    _dump_config_info(_PERSISTENT_CONFIG_PATH, dst)
