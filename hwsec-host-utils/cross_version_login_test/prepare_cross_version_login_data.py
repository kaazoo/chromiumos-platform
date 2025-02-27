#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This script is used for creating the cross-version login testing data.

Given the target CrOS version, the script will download the image from google
storage and create user account in the image. Then, copy and upload the data to
google storage so that we could use it in cross-version login testing.
"""

import argparse
import hashlib
import logging
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from typing import List, NamedTuple, Optional

import util


VM_HOST = "127.0.0.1"
VM_PORT = "9222"
SSH_COMMON_ARGS = (
    "-o",
    "StrictHostKeyChecking=no",
    "-o",
    "GlobalKnownHostsFile=/dev/null",
    "-o",
    "UserKnownHostsFile=/dev/null",
    "-o",
    "LogLevel=quiet",
)
HSM_BOARD_MAPPING = {
    "tpm2": "amd64-generic",
    "ti50": "betty",
    "tpm_dynamic": "reven-vmtest",
}


class Version(NamedTuple):
    """Represents a parsed ChromiumOS version."""

    milestone: int
    major: int
    minor: int
    patch: int
    # An optional suffix in the "-custombuildYYYYMMDD" format.
    custombuild: str

    def __str__(self) -> str:
        return (
            f"R{self.milestone}-{self.major}.{self.minor}.{self.patch}"
            f"{self.custombuild}"
        )


class Board(NamedTuple):
    """Represents the information of a ChromiumOS board."""

    name: str
    hsm: Optional[str] = None


class ImageInfo(NamedTuple):
    """Represents the information of ChromiumOS image"""

    board: Board
    version: Version

    def gs_url(self) -> str:
        """Fetches the ChromiumOS image from Google Cloud Storage."""
        if self.version.custombuild:
            # Download the custom-built VM image from a special GS folder (which
            # is populated manually by developers).
            image_url = (
                f"gs://chromeos-test-assets-private/tast/cros/hwsec/"
                f"cross_version_login/custombuilds/"
                f"{self.version}_{self.board.name}.tar.xz"
            )
        else:
            # No "custombuild" in the specified version, hence use the standard
            # GS folder (it's populated by build bots).
            gs_dir = board_gs_dir(self.board.name)
            image_url = f"{gs_dir}/{self.version}/chromiumos_test_image.tar.xz"
        return image_url


class DUT:
    """Handles commands related to DUT"""

    def __init__(self, ssh_identity: Path):
        self.ssh_identity = ssh_identity

    def copy(self, remote_path: Path, local_path: Path) -> None:
        """Fetches a file from the DUT."""
        util.check_run(
            "scp",
            *SSH_COMMON_ARGS,
            "-i",
            self.ssh_identity,
            "-P",
            VM_PORT,
            f"root@{VM_HOST}:{remote_path}",
            local_path,
        )

    def run(self, command: str) -> None:
        """Runs the command on the DUT remotely."""
        util.check_run(
            "ssh",
            *SSH_COMMON_ARGS,
            "-i",
            self.ssh_identity,
            "-p",
            VM_PORT,
            f"root@{VM_HOST}",
            command,
        )


def main(argv: Optional[List[str]] = None) -> Optional[int]:
    parser = argparse.ArgumentParser(
        description="Generate cross-version test login data"
    )
    parser.add_argument(
        "--debug",
        help="shows debug logging",
        action="store_true",
    )

    version_group = parser.add_argument_group(
        "ChromiumOS version"
    ).add_mutually_exclusive_group(required=True)
    version_group.add_argument(
        "--versions",
        help="ChromiumOS version(s), e.g., R100-14526.89.0 or "
        "R100-14526.89.0-custombuild20220130",
        nargs="+",
    )
    version_group.add_argument(
        "--milestones",
        help="ChromiumOS milstone(s), e.g., 100. The latest version of the "
        "specified milestone is used.",
        nargs="+",
        type=int,
    )

    board_group = parser.add_argument_group(
        "ChromiumOS board"
    ).add_mutually_exclusive_group(required=True)
    board_group.add_argument(
        "--boards",
        help="ChromiumOS board(s), e.g., betty",
        nargs="+",
    )
    board_group.add_argument(
        "--hsms",
        help="chooses board(s) by hardware security module(s). Available "
        "options are 'tpm2', 'ti50', 'tpm_dynamic'",
        nargs="+",
    )

    output_group = parser.add_argument_group(
        "Output options"
    ).add_mutually_exclusive_group(required=True)
    output_group.add_argument(
        "--output-dir",
        help="path to the directory to place output files in. If --hsms is "
        "specified, the file is put in corresponding subdirectory.",
        type=Path,
    )
    output_group.add_argument(
        "--no-output",
        help="don't output or upload anything",
        action="store_true",
    )
    output_group.add_argument(
        "--output-tast",
        help="Same as `--output-dir` but writes output to the directory "
        "containing the current x-ver data in tast-tests",
        action="store_true",
    )

    parser.add_argument(
        "--ssh-identity-file",
        help="path to the SSH private key file",
        type=Path,
    )
    opts = parser.parse_args(argv)

    util.init_logging(opts.debug)

    boards = []
    if opts.boards:
        boards = [Board(name=board) for board in opts.boards]
    else:
        for hsm in opts.hsms:
            if hsm not in HSM_BOARD_MAPPING:
                raise ValueError(f"Invalid hardware security module: '{hsm}'")
            boards.append(Board(hsm=hsm, name=HSM_BOARD_MAPPING[hsm]))

    # First, ensure the existence of image(s) to use.
    image_info_list = []
    for board in boards:
        versions = opts.versions
        if not versions:
            versions = [
                get_latest_version(board.name, milestone)
                for milestone in opts.milestones
            ]
        for version_str in versions:
            image_info = ImageInfo(board, parse_version(version_str))
            ensure_vm_image(image_info)
            image_info_list.append(image_info)

    output_dir = None
    if opts.output_dir:
        output_dir = opts.output_dir
    elif opts.output_tast:
        output_dir = util.chromiumos_src() / Path(
            "platform/tast-tests/src/go.chromium.org/tast-tests/cros/"
            "local/bundles/cros/hwsec/fixture/data/cross_version_login"
        )

    for image_info in image_info_list:
        run(image_info, output_dir, opts.ssh_identity_file)


def run(
    image_info: ImageInfo,
    output_dir: Optional[Path],
    ssh_identity_file: Optional[Path],
) -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        if image_info.board.hsm and output_dir:
            output_dir /= image_info.board.hsm
            output_dir.mkdir(exist_ok=True)
        temp_path = Path(temp_dir)
        ssh_identity = setup_ssh_identity(ssh_identity_file, temp_path)
        dut = DUT(ssh_identity)
        image_path = download_vm_image(image_info, temp_path)
        start_vm(image_path, image_info.board.name)
        try:
            init_vm(image_info.version, dut)
            generate_data()
            if output_dir:
                upload_data(
                    image_info.board.name,
                    image_info.version,
                    output_dir,
                    temp_path,
                    dut,
                )
        finally:
            stop_vm()


def parse_version(version: str) -> Version:
    """Parses a ChromeOS version string, like "R100-14526.89.0".

    Also allows the "R100-14526.89.0-custombuild20220130" format.
    """
    match = re.fullmatch(
        r"R(\d+)-(\d+)\.(\d+)\.(\d+)(-custombuild\d{8}|)", version
    )
    if not match:
        raise RuntimeError(f"Failed to parse version {version}")
    return Version(
        int(match.group(1)),
        int(match.group(2)),
        int(match.group(3)),
        int(match.group(4)),
        match.group(5),
    )


def board_gs_dir(board: str) -> str:
    # Folder of *-generic VM board builds from builders, which are not
    # built by chromeos release builders.
    if board.endswith("-generic"):
        return f"gs://chromiumos-image-archive/{board}-vm-public"
    # Folder of standard builds from chromeos release builders.
    return f"gs://chromeos-image-archive/{board}-release"


def get_latest_version(board: str, milestone: int) -> str:
    gs_dir = board_gs_dir(board)
    gs_url = f"{gs_dir}/LATEST-release-R{milestone}*"
    version_str = util.check_run("gsutil", "cat", gs_url).decode("utf-8")
    return version_str


def setup_ssh_identity(
    ssh_identity_file: Optional[Path], temp_path: Path
) -> Path:
    """Copies the SSH identity file and fixes the permissions."""
    source_path = (
        ssh_identity_file if ssh_identity_file else default_ssh_identity_file()
    )
    target_path = Path(f"{temp_path}/ssh_key")
    shutil.copy(source_path, target_path)
    # Permissions need to be adjusted to prevent ssh complaints.
    target_path.chmod(stat.S_IREAD)
    return target_path


def default_ssh_identity_file() -> Path:
    home = Path.home()
    return Path(f"{home}/chromiumos/chromite/ssh_keys/testing_rsa")


def ensure_vm_image(image_info: ImageInfo):
    image_url = image_info.gs_url()
    try:
        util.check_run("gsutil", "ls", image_url)
    except subprocess.CalledProcessError:
        raise RuntimeError(
            "Failed to find the VM image. Please check the validity of the"
            "given version and board"
        )


def download_vm_image(image_info: ImageInfo, temp_path: Path) -> Path:
    image_url = image_info.gs_url()
    archive_path = Path(f"{temp_path}/chromiumos_test_image.tar.xz")
    util.check_run("gsutil", "cp", image_url, archive_path)
    # Unpack the .tar.xz archive.
    util.check_run("tar", "Jxf", archive_path, "-C", temp_path)
    target_path = Path(f"{temp_path}/chromiumos_test_image.bin")
    if not target_path.exists():
        raise RuntimeError(f"No {target_path} in VM archive")
    return target_path


def start_vm(image_path: Path, board: str) -> None:
    """Runs the VM emulator."""
    util.check_run(
        "cros",
        "vm",
        "--log-level=warning",
        "--start",
        "--image-path",
        image_path,
        "--board",
        board,
    )


def stop_vm() -> None:
    """Stops the VM emulator."""
    util.check_run("cros", "vm", "--stop")


def init_vm(version: Version, dut: DUT) -> None:
    """Makes sure the VM is in the right state for collecting the state."""
    if version.milestone < 96:
        # Normally the Tast framework takes care of the TPM ownership, however
        # it doesn't support pre-M96 images (as Tast ToT uses the
        # "get_supported_features" tpm_manager command that was added later).
        dut.run("tpm_manager_client take_ownership")


def generate_data() -> None:
    """Generates a data snapshot by running the Tast test."""
    # "tpm2_simulator" is added by crrev.com/c/3312977, so this test cannot run
    # on older version. Therefore, adds -extrauseflags "tpm2_simulator" here.
    util.check_run(
        "cros_sdk",
        "tast",
        "run",
        "-failfortests",
        "-extrauseflags",
        "tpm2_simulator",
        f"{VM_HOST}:{VM_PORT}",
        "hwsec.PrepareCrossVersionLoginData",
    )


def upload_data(
    board: str,
    version: Version,
    output_dir: Path,
    temp_path: Path,
    dut: DUT,
) -> None:
    """Creates resulting artifacts and uploads to the GS."""
    DUT_ARTIFACTS_DIR = "/tmp/cross_version_login"
    date = time.strftime("%Y%m%d")
    prefix = f"{version}_{board}_{date}"
    # Grab the data file from the DUT.
    data_file = f"{prefix}_data.tar.gz"
    dut_data_path = Path(f"{DUT_ARTIFACTS_DIR}/data.tar.gz")
    data_path = Path(f"{temp_path}/{data_file}")
    dut.copy(dut_data_path, data_path)
    # Grab the config file from the DUT.
    dut_config_path = Path(f"{DUT_ARTIFACTS_DIR}/config.json")
    config_path = Path(f"{output_dir}/{prefix}_config.json")
    dut.copy(dut_config_path, config_path)
    logging.info('Config file is created at "%s".', config_path)
    # Generate the external data file that points to the file in GS.
    gs_url = (
        f"gs://chromiumos-test-assets-public/tast/cros/hwsec/"
        f"cross_version_login/{data_file}"
    )
    external_data = generate_external_data(gs_url, data_path)
    external_data_path = Path(f"{output_dir}/{data_file}.external")
    with open(external_data_path, "w", encoding="utf-8") as f:
        f.write(external_data)
    logging.info('External data file is created at "%s".', external_data_path)
    # Upload the data file to Google Cloud Storage.
    util.check_run("gsutil", "cp", data_path, gs_url)
    logging.info("Testing data is uploaded to %s.", gs_url)


def generate_external_data(gs_url: str, data_path: Path) -> str:
    """Generates external data in the Tast format (JSON)."""
    st = data_path.stat()
    sha256 = calculate_file_sha256(data_path)
    return f"""{{
  "url": "{gs_url}",
  "size": {st.st_size},
  "sha256sum": "{sha256}"
}}
"""


def calculate_file_sha256(path: Path) -> str:
    READ_SIZE = 4096
    sha256 = hashlib.sha256()
    with open(path, "rb") as infile:
        while True:
            block = infile.read(READ_SIZE)
            if not block:
                break
            sha256.update(block)
    return sha256.hexdigest()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
