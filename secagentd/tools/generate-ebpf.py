#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Python script to generate ebpf skeletons from bpf code.

This script compiles the C code with target bpf and then runs bpftool against
the resulting object file to generate bpf skeleton header files that can then be
used by userspace programs to load, attach and communicate with bpf functions.
"""

import argparse
import os
import pathlib
import re
import subprocess
import sys
import typing


def _run_command(command: typing.List[str]) -> subprocess.CompletedProcess:
    """Run a command with default options.

    Run a command using subprocess.run with default configuration.
    """
    return subprocess.run(
        command,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        encoding="utf-8",
    )


def do_gen_bpf_skeleton(args):
    """Generate BPF skeletons from C.

    Takes a BPF application written in C and generates the BPF object file and
    then uses that to generate BPF skeletons using bpftool.
    If args.out_min_btf is specified, the BPF object file is also processed to
    generate a min CO-RE BTF.
    """
    out_header = args.out_header
    out_btf = args.out_min_btf
    vmlinux_btf = args.vmlinux_btf
    source = args.source
    arch = args.arch
    includes = args.includes
    defines = args.defines or []
    sysroot = args.sysroot

    obj = "_".join(os.path.basename(source).split(".")[:-1]) + ".o"

    arch_to_define = {
        "amd64": "__TARGET_ARCH_x86",
        "amd64-linux": "__TARGET_ARCH_x86",
        "arm": "__TARGET_ARCH_arm",
        "arm-linux": "__TARGET_ARCH_arm",
        "arm64": "__TARGET_ARCH_arm64",
        "mips": "__TARGET_ARCH_mips",
        "ppc": "__TARGET_ARCH_powerpc",
        "ppc64": "__TARGET_ARCH_powerpc",
        "ppc64-linux": "__TARGET_ARCH_powerpc",
        "x86": "__TARGET_ARCH_x86",
        "x86-linux": "__TARGET_ARCH_x86",
    }
    arch = arch_to_define.get(arch, None)
    if arch is None:
        print(
            f"Unable to map arch={arch} to a sensible"
            "__TARGET_ARCH_XXX for bpf compilation."
        )
        return -1

    # Calling bpf-clang is equivalent to "clang --target bpf".
    # It may seem odd that the application needs to be compiled with -g but
    # then llvm-strip is ran against the resulting object.
    # The -g is needed for the bpf application to compile properly but we
    # want to reduce the file size by stripping it.
    call_bpf_clang = (
        ["/usr/bin/bpf-clang", "-g", "-O2", f"--sysroot={sysroot}"]
        + [f"-I{x}" for x in includes]
        + [f"-D{x}" for x in defines]
        + [f"-D{arch}", "-c", source, "-o", obj]
    )
    gen_skeleton = ["/usr/sbin/bpftool", "gen", "skeleton", obj]
    strip_dwarf = ["llvm-strip", "-g", obj]

    # Compile the BPF C application.
    _run_command(call_bpf_clang)
    # Strip useless dwarf information.
    _run_command(strip_dwarf)
    # Use bpftools to generate skeletons from the BPF object files.
    bpftool_proc = _run_command(gen_skeleton)

    # BPFtools will output the C formatted dump of kernel symbols to stdout.
    # Write the contents to file.
    with open(out_header, "w", encoding="utf-8") as bpf_skeleton:
        bpf_skeleton.write(bpftool_proc.stdout)

    # Generate a detached min_core BTF.
    if out_btf:
        if not vmlinux_btf:
            print(
                "Need a full vmlinux BTF as input in order to generate a min "
                "BTF"
            )
            return -1
        gen_min_core_btf = [
            "/usr/sbin/bpftool",
            "gen",
            "min_core_btf",
            vmlinux_btf,
            out_btf,
            obj,
        ]
        _run_command(gen_min_core_btf)

    return 0


def do_gen_vmlinux(args):
    """Generate vmlinux.h for use in BPF programs.

    Invokes pahole and bpftool to generate vmlinux.h from vmlinux from the
    kernel build. Uses BTF as an intermediate format. The generated BTF is
    preserved for possible use in generation of min CO-RE BTFs.
    """
    sysroot = args.sysroot
    vmlinux_out = args.out_header
    btf_out = args.out_btf
    vmlinux_in = f"{sysroot}/usr/lib/debug/boot/vmlinux"
    gen_detached_btf = [
        "/usr/bin/pahole",
        "--btf_encode_detached",
        btf_out,
        vmlinux_in,
    ]
    gen_vmlinux = [
        "/usr/sbin/bpftool",
        "btf",
        "dump",
        "file",
        btf_out,
        "format",
        "c",
    ]
    read_symbols = [
        "/usr/bin/readelf",
        "--symbols",
        "--wide",
        vmlinux_in,
    ]
    read_sections = [
        "/usr/bin/readelf",
        "--sections",
        "--wide",
        vmlinux_in,
    ]

    # First, run pahole to generate a detached vmlinux BTF. This step works
    # regardless of whether the vmlinux was built with CONFIG_DEBUG_BTF_INFO.
    pathlib.Path(os.path.dirname(btf_out)).mkdir(parents=True, exist_ok=True)
    _run_command(gen_detached_btf)

    symbols = _run_command(read_symbols)
    for line in symbols.stdout.splitlines():
        # Example line to parse:
        # 145612: ffffffff823e1ca0   274 OBJECT  GLOBAL DEFAULT    2 linux_banner
        d = line.split()
        if len(d) == 8 and d[-1] == "linux_banner":
            linux_banner_addr = int(d[1], 16)
            linux_banner_len = int(d[2])
            linux_banner_sec = d[-2]
            break
    else:
        print(f"Failed to find linux_banner from {vmlinux_in}")
        return 1

    sections = _run_command(read_sections)
    for line in sections.stdout.splitlines():
        # Example line to parse:
        # [ 2] .rodata           PROGBITS        ffffffff82200000 1400000 5e104e 00 WAMS  0   0 4096
        if line.lstrip().startswith(f"[{linux_banner_sec:>2}]"):
            d = line.split("]", 1)[1].split()
            section_addr = int(d[2], 16)
            section_off = int(d[3], 16)
            break
    else:
        print(
            f"Failed to find section '[{linux_banner_sec:>2}]' from "
            f"{vmlinux_in}"
        )
        return 1

    offset = section_off + linux_banner_addr - section_addr
    with open(vmlinux_in, "rb") as fp:
        fp.seek(offset)
        linux_banner = fp.read(linux_banner_len).decode("utf-8")

    m = re.match(r"Linux version (\d+).(\d+).(\d+)", linux_banner)
    if not m:
        print(f"Failed to match Linux version: {linux_banner}")
        return 1

    version = m.group(1)
    patch_level = m.group(2)
    sub_level = m.group(3)

    # Constructing LINUX_VERSION_CODE.
    linux_version_code = int(version) * 65536 + int(patch_level) * 256
    linux_version_code += min(int(sub_level), 255)

    # Then, use the generated BTF (and not vmlinux itself) to generate the
    # header.
    vmlinux_cmd = _run_command(gen_vmlinux)
    with open(f"{vmlinux_out}", "w", encoding="utf-8") as vmlinux:
        written = False

        for line in vmlinux_cmd.stdout.splitlines():
            vmlinux.write(f"{line}\n")

            if not written and line == "#define __VMLINUX_H__":
                vmlinux.write(
                    f"\n#define LINUX_VERSION_CODE {linux_version_code}\n"
                )
                written = True

        if not written:
            print("Failed to write LINUX_VERSION_CODE")
            return 1

    return 0


def main(argv: typing.List[str]) -> int:
    """A command line tool for all things BPF.

    A command line tool to help generate C BPF skeletons and to generate
    vmlinux.h from kernel build artifacts.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(help="sub-command help")

    gen_skel = subparsers.add_parser("gen_skel")
    gen_skel.add_argument(
        "--out_header",
        required=True,
        help="The name of the output header file.",
    )
    gen_skel.add_argument(
        "--source", required=True, help="The bpf source code."
    )
    gen_skel.add_argument(
        "--arch", required=True, help="The target architecture."
    )
    gen_skel.add_argument(
        "--includes",
        required=True,
        nargs="+",
        help="Additional include directories.",
    )
    gen_skel.add_argument(
        "--defines",
        required=False,
        nargs="*",
        help="Additional preprocessor defines.",
    )
    gen_skel.add_argument(
        "--vmlinux_btf",
        required=False,
        help="The detached full vmlinux BTF file.",
    )
    gen_skel.add_argument(
        "--out_min_btf",
        required=False,
        help="The name of the output min BTF file.",
    )
    # We require the board sysroot so that BPF compilations will use board
    # libbpf headers.
    gen_skel.add_argument(
        "--sysroot",
        required=True,
        help="The path that should be treated as the root directory.",
    )

    gen_skel.set_defaults(func=do_gen_bpf_skeleton)

    gen_vmlinux = subparsers.add_parser("gen_vmlinux")
    gen_vmlinux.add_argument(
        "--sysroot",
        required=True,
        help="The path that should be treated as the root directory.",
    )
    gen_vmlinux.add_argument(
        "--out_header",
        required=True,
        help="The name of the output vmlinux.h file.",
    )
    gen_vmlinux.add_argument(
        "--out_btf",
        required=True,
        help="The name of the output vmlinux BTF file.",
    )
    gen_vmlinux.set_defaults(func=do_gen_vmlinux)
    args = parser.parse_args(argv)

    try:
        return args.func(args)
    except subprocess.CalledProcessError as error:
        print(
            f'cmd={" ".join(error.cmd)}\nstderr={error.stderr}\n'
            f"stdout={error.stdout}\nretcode={error.returncode}\n"
        )
        return -1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
