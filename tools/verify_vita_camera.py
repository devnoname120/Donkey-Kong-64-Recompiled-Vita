#!/usr/bin/env python3
"""Verify archived camera-probe captures and an optional fresh-process save reload.

The run directories must contain progress.log, input.json, process.json,
config.yml, captured readback/photo bytes and saves-after/DK64.bin. No ROM or
save data is distributed with this tool. A timed emulator exit is reported
separately from the game-level assertions.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

from compare_vita_photograph import sepia


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_info(directory):
    info = json.loads((directory / "input.json").read_text())
    config = (directory / "config.yml").read_text()
    require("backend-renderer: Vulkan" in config, "run did not select Vulkan")
    require("memory-mapping: external-host" in config, "unexpected readback mapping")
    require(f"cpu-opt: {str(info['cpu_opt']).lower()}" in config,
            "CPU optimization setting disagrees with the run manifest")
    require(digest(directory / "DK64AdventureProbe.vpk") == info["vpk_sha256"],
            "archived VPK differs from the run manifest")
    text = (directory / "progress.log").read_text()
    require("Unhandled runtime exception" not in text, "game raised an exception")
    info["emulator_exit_code"] = json.loads((directory / "process.json").read_text())["exit_code"]
    info["save_after_sha256"] = digest(directory / "saves-after" / "DK64.bin")
    return info, text


def photograph(source_path, photo_path):
    raw_source, raw_photo = source_path.read_bytes(), photo_path.read_bytes()
    require(len(raw_source) == 153600 and len(raw_photo) == 40960,
            "incorrect framebuffer or photograph size")
    source, photo = struct.unpack(">76800H", raw_source), struct.unpack(">20480H", raw_photo)
    matches = 0
    for ty in range(2):
        for tx in range(5):
            for y in range(64):
                for x in range(32):
                    index = ((ty * 5 + tx) * 64 + y) * 32 + x
                    expected = sepia(source[(56 + ty * 64 + y) * 320 + 80 + tx * 32 + x])
                    matches += photo[index] == expected
    require(matches == len(photo), f"{photo_path.name}: {len(photo) - matches} incorrect pixels")
    return {"pixels": len(photo), "matches": matches,
            "framebuffer_sha256": digest(source_path), "photograph_sha256": digest(photo_path)}


def capture(directory):
    info, text = run_info(directory)
    require("Camera probe prerequisites: camera=1 film=10 fairy_flag=0" in text,
            "capture did not start with an uncaptured fairy and ten film")
    pending = None
    images = []
    state = None
    completed = False
    for line in text.splitlines():
        read = re.search(r"Game framebuffer readback (\d+): address=([0-9a-f]{8}) requested=(\d+) returned=(\d+) written=(\d+)", line)
        if read:
            require(all(int(read[i]) == 153600 for i in (3, 4, 5)), "incomplete game readback")
            pending = directory / f"readback-{read[1]}-{read[2]}.bin"
        photo = re.search(r"Camera probe picture: index=(\d+).* result=(\d+)", line)
        if photo:
            index, result = int(photo[1]), int(photo[2])
            require(index == len(images) and index < 2 and pending is not None,
                    "photograph is missing its ordered source readback")
            require((result == 1) == (index == 1), "miss/success sequence changed")
            images.append(photograph(pending, directory / f"camera-photo-{index}.rgba16"))
            pending = None
        frame = re.search(r"Camera probe frame=\d+ .* state=(\d+) .* captured=(\d+)", line)
        if frame:
            state = (int(frame[1]), int(frame[2]))
        if re.search(r"Camera probe completed: photos=2 film=\d+ fairy_flag=1 queued=3 freed=3", line):
            require(state in ((12, 1), (13, 1)), "probe declared completion before normal control returned")
            completed = True
    require(completed and len(images) == 2, "camera lifecycle did not complete")
    require(info["save_after_sha256"] != info["saves"]["DK64.bin"], "capture did not change the save")
    return {"vpk_sha256": info["vpk_sha256"], "cpu_opt": info["cpu_opt"],
            "emulator_exit_code": info["emulator_exit_code"],
            "save_after_sha256": info["save_after_sha256"], "photographs": images}


def restart(directory, captured):
    info, text = run_info(directory)
    require(info["vpk_sha256"] == captured["vpk_sha256"], "restart did not use the same VPK")
    require(info["saves"]["DK64.bin"] == captured["save_after_sha256"], "restart used a different save")
    require(info.get("verify_save") is True, "restart verification was not selected")
    require(re.search(r"Camera probe save verification passed: camera=1 fairy_flag=1 film=\d+; no prerequisites changed", text),
            "saved fairy progress was not observed by the original flag API")
    for forbidden in ("Camera probe prerequisites:", "Camera probe fixture:", "Camera probe picture:"):
        require(forbidden not in text, "restart changed test prerequisites or repeated the capture")
    return {"verified": True, "cpu_opt": info["cpu_opt"],
            "input_save_sha256": info["saves"]["DK64.bin"],
            "emulator_exit_code": info["emulator_exit_code"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_run", type=Path)
    parser.add_argument("--restart", type=Path)
    args = parser.parse_args()
    try:
        result = {"capture": capture(args.capture_run)}
        if args.restart:
            result["restart"] = restart(args.restart, result["capture"])
        print(json.dumps(result, indent=2))
        return 0
    except (ValueError, OSError, KeyError, struct.error) as error:
        parser.exit(1, f"Camera validation failed: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
