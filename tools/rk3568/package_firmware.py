#!/usr/bin/env python3
"""Install a staged VeloraIME package into a copy of the RK3568 firmware."""

import argparse
import filecmp
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile


SDK = Path.home() / "rk3568/workspace/atk_dlrk3568_linux6.1"
INIT_SCRIPT = Path(__file__).resolve().with_name("S95veloraime")
FIRMWARE_FILES = ("MiniLoaderAll.bin", "boot.img", "misc.img", "oem.img",
                  "parameter.txt", "recovery.img", "uboot.img", "userdata.img")
MODULE = Path("usr/share/veloraime/qml/Rk/TouchKeyboard")


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def check_arm64(path):
    with path.open("rb") as binary:
        header = binary.read(20)
    if not (header[:6] == b"\x7fELF\x02\x01" and header[18:20] == b"\xb7\x00"):
        raise ValueError(f"{path} must be an AArch64 ELF")


def add_installation(stage, rootfs, work):
    init_destination = "etc/init.d/S95veloraime"
    files = [(path, str(path.relative_to(stage))) for path in (stage / "usr").rglob("*")
             if path.is_file()]
    files.append((INIT_SCRIPT, init_destination))
    directories = {Path(name).parent for _, name in files}
    directories.update(parent for directory in tuple(directories) for parent in directory.parents)
    commands = []
    for directory in sorted(directories - {Path(".")}, key=lambda path: (len(path.parts), str(path))):
        name = f"/{directory}"
        if not re.fullmatch(r"/[\w./+-]+", name):
            raise ValueError(f"Unsupported firmware path {name}")
        result = run("debugfs", "-R", f"stat {name}", rootfs, capture_output=True, text=True)
        if "Type: directory" not in result.stdout:
            commands.append(f"mkdir {name}")

    for number, (source, name) in enumerate(files):
        if not re.fullmatch(r"(usr|etc)/[\w./+-]+", name):
            raise ValueError(f"Unsupported firmware path {name}")
        temporary = f"install-{number}"
        shutil.copyfile(source, work / temporary)
        mode = 0o755 if name == init_destination else stat.S_IMODE(source.stat().st_mode)
        current = run("debugfs", "-R", f"stat /{name}", rootfs, capture_output=True, text=True)
        if "Inode:" in current.stdout:
            if "Type: regular" not in current.stdout:
                raise ValueError(f"Image contains unexpected file type at /{name}")
            commands.append(f"rm /{name}")
        commands.extend([f"write {temporary} /{name}",
                         f"set_inode_field /{name} mode {0o100000 | mode:07o}"])

    command_file = work / "install.debugfs"
    command_file.write_text("\n".join(commands) + "\n", encoding="utf-8")
    run("debugfs", "-w", "-f", command_file, rootfs, cwd=work)
    for number, (source, name) in enumerate(files):
        check = work / f"verify-{number}"
        run("debugfs", "-R", f"dump -p /{name} {check.name}", rootfs, cwd=work,
            capture_output=True)
        if not filecmp.cmp(check, source, shallow=False):
            raise ValueError(f"Image does not contain the expected /{name}")
        expected_mode = 0o755 if name == init_destination else stat.S_IMODE(source.stat().st_mode)
        if stat.S_IMODE(check.stat().st_mode) != expected_mode:
            raise ValueError(f"Image has an unexpected mode for /{name}")
    run("e2fsck", "-fn", rootfs)


def package(sdk, stage, output):
    firmware = sdk / "output/firmware"
    packer = sdk / "tools/linux/Linux_Pack_Firmware/rockdev"
    for binary in ("usr/bin/veloraime-demo", MODULE / "libVeloraIME.so",
                   MODULE / "libVeloraIMEPlugin.so"):
        check_arm64(stage / binary)
    if not (stage / MODULE / "qmldir").is_file():
        raise ValueError("QML import metadata is missing")
    if not output.parent.is_dir() or output.exists():
        raise ValueError("Use a new output file in an existing directory")

    with tempfile.TemporaryDirectory(prefix="velora-rk3568-", dir=output.parent) as temporary:
        work = Path(temporary)
        image = work / "Image"
        image.mkdir()
        rootfs = image / "rootfs.img"
        run("cp", "--reflink=auto", "--sparse=always", firmware / "rootfs.img", rootfs)
        add_installation(stage, rootfs, work)

        for name in FIRMWARE_FILES:
            (image / name).symlink_to((firmware / name).resolve(strict=True))
        shutil.copyfile(sdk / "output/update/Image/package-file", image / "package-file")
        loader = image / "MiniLoaderAll.bin"
        with loader.open("rb") as stream:
            stream.seek(21)
            tag = stream.read(4)[::-1].decode("ascii")
        if not re.fullmatch(r"[0-9A-Z]{4}", tag):
            raise ValueError("Invalid Rockchip loader tag")
        raw = work / "update.raw.img"
        combined = work / "update.img"
        run(packer / "afptool", "-pack", "./", raw, cwd=image)
        run(packer / "rkImageMaker", f"-RK{tag}", loader, raw, combined, "-os_type:androidos")
        with combined.open("rb") as stream:
            if stream.read(4) != b"RKFW":
                raise ValueError("Invalid final firmware header")
        os.link(combined, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", default=SDK, type=Path)
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    package(args.sdk.resolve(), args.stage.resolve(), args.output.resolve())


if __name__ == "__main__":
    main()
