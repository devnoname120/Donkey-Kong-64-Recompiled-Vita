from __future__ import annotations

import argparse
import csv
import fcntl
import ftplib
import hashlib
import io
import json
import math
import re
import socket
import statistics
import subprocess
import time
import uuid
import zipfile
from collections import defaultdict
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

TITLE = "DK64RE001"
EBOOT = f"/ux0:/app/{TITLE}/eboot.bin"
DATA = "/ux0:/data/dk64recompiled-benchmark"
SAVES = "/ux0:/data/dk64recompiled/saves"
CORE_DATA = "/ux0:/data"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, value: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".part")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


class Device:
    def __init__(self, host: str):
        self.host = host
        self._connection = None

    def __enter__(self):
        return self

    def __exit__(self, *exception):
        self.close()

    def close(self, abort: bool = False) -> None:
        ftp, self._connection = self._connection, None
        if ftp is not None:
            try:
                if not abort:
                    ftp.quit()
            except ftplib.all_errors:
                pass
            finally:
                ftp.close()

    @contextmanager
    def ftp(self):
        if self._connection is None:
            ftp = ftplib.FTP()
            try:
                ftp.connect(self.host, 1337, timeout=15)
                ftp.login()
                ftp.sock.settimeout(120)
            except BaseException:
                ftp.close()
                raise
            self._connection = ftp
        try:
            yield self._connection
        except (OSError, EOFError, ftplib.error_temp, ftplib.error_proto, ftplib.error_reply):
            self.close(abort=True)
            raise

    def command(self, command: str) -> str:
        if "\n" in command or "\r" in command or len(command) > 512:
            raise ValueError("Invalid command framing")
        with socket.create_connection((self.host, 1338), timeout=10) as connection:
            connection.settimeout(20)
            connection.sendall((command + "\n").encode("ascii"))
            response = bytearray()
            while True:
                chunk = connection.recv(4096)
                if not chunk:
                    break
                response.extend(chunk)
                if len(response) > 16384:
                    raise ValueError("Oversized VitaCompanion response")
        return response.decode("utf-8", errors="replace")

    def kill(self) -> None:
        print("stop:", self.command(f"kill {TITLE}").strip(), flush=True)

    def launch(self) -> None:
        self.command("screen on")
        response = self.command(f"launch {TITLE}")
        if response.strip() != "Launched.":
            raise RuntimeError(response)
        print("launch:", response.strip(), flush=True)

    def exists(self, path: str) -> bool:
        try:
            with self.ftp() as ftp:
                previous = ftp.sock.gettimeout()
                try:
                    ftp.sock.settimeout(5)
                    ftp.voidcmd("TYPE I")
                    size = ftp.size(path)
                    return size is not None
                finally:
                    if ftp.sock is not None:
                        ftp.sock.settimeout(previous)
        except ftplib.error_perm as error:
            if str(error).startswith("550"):
                return False
            raise

    def get(self, path: str, limit: int = 64 * 1024 * 1024) -> bytes:
        result = bytearray()

        def receive(chunk: bytes) -> None:
            if len(result) + len(chunk) > limit:
                raise ValueError(f"Remote file exceeds limit: {path}")
            result.extend(chunk)

        for attempt in range(3):
            result.clear()
            try:
                with self.ftp() as ftp:
                    ftp.retrbinary("RETR " + path, receive, blocksize=65536)
                return bytes(result)
            except ftplib.error_perm as error:
                if str(error).startswith("550"):
                    raise FileNotFoundError(path) from error
                raise
            except (OSError, EOFError, ftplib.error_temp, ftplib.error_proto, ftplib.error_reply):
                if attempt == 2:
                    raise
                time.sleep(1)
        raise RuntimeError("Unreachable FTP retry state")

    def put(self, path: str, data: bytes) -> None:
        with self.ftp() as ftp:
            ftp.storbinary("STOR " + path, io.BytesIO(data), blocksize=65536)
        if self.get(path) != data:
            raise RuntimeError(f"Upload readback differs: {path}")

    def rename(self, source: str, target: str) -> None:
        with self.ftp() as ftp:
            ftp.rename(source, target)

    def mkdir(self, path: str) -> None:
        with self.ftp() as ftp:
            try:
                ftp.mkd(path)
            except ftplib.error_perm:
                ftp.cwd(path)

    def list(self, path: str) -> list[str]:
        with self.ftp() as ftp:
            return [name.rsplit("/", 1)[-1] for name in ftp.nlst(path + "/")]

    def core_inventory(self) -> dict[str, dict]:
        with self.ftp() as ftp:
            return {name: facts for name, facts in ftp.mlsd(CORE_DATA + "/")
                    if facts.get("type") == "file" and name.startswith("psp2core-")
                    and "/" not in name and "\\" not in name}


@contextmanager
def temporary_executable(device, payload: bytes, original: bytes, output: Path):
    if device.get(EBOOT) != original:
        raise ValueError("Unexpected installed executable; refusing to replace it")
    token = uuid.uuid4().hex[:12]
    staged, backup, retired = (EBOOT + suffix + token for suffix in (".pending-", ".original-", ".benchmark-"))
    report = {"original_sha256": digest(original), "benchmark_sha256": digest(payload),
              "backup": backup, "staged": staged, "retired": retired,
              "installed": False, "restored": False}
    (output / "original-eboot.bin").write_bytes(original)
    write_json(output / "deployment.json", report)
    device.put(staged, payload)
    if device.get(staged) != payload:
        raise RuntimeError("Staged benchmark failed readback verification; original executable unchanged")
    device.kill()
    if device.get(EBOOT) != original:
        raise ValueError("The installed executable changed before replacement")
    device.rename(EBOOT, backup)
    try:
        device.rename(staged, EBOOT)
        if device.get(EBOOT) != payload:
            raise RuntimeError("Installed benchmark failed readback verification")
        report["installed"] = True
        write_json(output / "deployment.json", report)
        yield
    finally:
        device.kill()
        try:
            current = device.get(EBOOT)
        except FileNotFoundError:
            current = None
        if current is not None and current != payload:
            report["restore_error"] = "Installed executable changed externally; original retained at backup"
            write_json(output / "deployment.json", report)
            raise RuntimeError(report["restore_error"])
        if current is not None:
            device.rename(EBOOT, retired)
        device.rename(backup, EBOOT)
        if device.get(EBOOT) != original:
            raise RuntimeError("Original executable restoration did not verify")
        report["restored"] = True
        write_json(output / "deployment.json", report)
        print("original executable restored and verified", flush=True)


def recover_executable(device, output: Path) -> None:
    path = output / "deployment.json"
    receipt = json.loads(path.read_text())
    try:
        current = device.get(EBOOT)
    except FileNotFoundError:
        current = None
    if current is not None and digest(current) == receipt["original_sha256"]:
        receipt["restored"] = True
        write_json(path, receipt)
        return
    if current is not None and digest(current) != receipt["benchmark_sha256"]:
        raise ValueError("Unrecognized installed executable; refusing recovery")
    original = device.get(receipt["backup"])
    if digest(original) != receipt["original_sha256"]:
        raise ValueError("Unrecognized original backup; refusing recovery")
    if current is not None:
        try:
            device.get(receipt["retired"])
        except FileNotFoundError:
            pass
        else:
            raise ValueError("Recovery retirement path already exists; refusing overwrite")
    device.kill()
    try:
        after_stop = device.get(EBOOT)
    except FileNotFoundError:
        after_stop = None
    if after_stop != current:
        raise ValueError("Unrecognized executable change during recovery")
    if current is not None:
        device.rename(EBOOT, receipt["retired"])
    device.rename(receipt["backup"], EBOOT)
    if device.get(EBOOT) != original:
        raise RuntimeError("Recovery restoration did not verify")
    receipt.update(restored=True, recovered_after_interruption=True)
    write_json(path, receipt)


def distribution(values: list[float]) -> dict[str, float | int]:
    if not values:
        return {"count": 0}
    ordered = sorted(values)
    return {"count": len(values), "median": statistics.median(values),
            "mean": statistics.mean(values), "p95": ordered[math.ceil(len(values) * 0.95) - 1],
            "p99": ordered[math.ceil(len(values) * 0.99) - 1], "max": ordered[-1]}


def summarize(rows: list[dict], profiles: list[dict], metadata: dict) -> dict:
    if metadata.get("schema") != 1 or not metadata.get("complete") or not metadata.get("valid"):
        raise ValueError("Capture is not complete and valid")
    if len(rows) != metadata.get("events"):
        raise ValueError("Capture event count differs from completion receipt")
    if len(profiles) != metadata.get("profiles") or metadata.get("dropped") or metadata.get("dropped_profiles"):
        raise ValueError("Capture has missing or dropped profiling records")
    grouped = defaultdict(list)
    for row in rows:
        grouped[row["kind"]].append(row)
    for values in grouped.values():
        values.sort(key=lambda row: row["time_us"])
    graphics, games, presents = grouped[2], grouped[1], grouped[3]
    if len(graphics) < 10 or len(games) < 10:
        raise ValueError("Too few game or graphics records to validate the workload")
    if metadata["scenario"] == "attract" and not any(row["map"] == 76 for row in graphics):
        raise ValueError("Attract fixture never reached the DK Rap")
    if metadata["scenario"] == "world" and not all(any(row["phase"] == p for row in graphics) for p in (2, 3, 4)):
        raise ValueError("World fixture missed a measured segment")

    def intervals(values):
        return [(b["time_us"] - a["time_us"]) / 1000 for a, b in zip(values, values[1:])]

    stages = defaultdict(float)
    counters = defaultdict(int)
    for profile in profiles:
        index = profile["event"]
        if not 0 <= index < len(rows) or not rows[index]["a"]:
            raise ValueError("Profile does not refer to a sampled event")
        if rows[index]["kind"] != 2:
            continue
        for key, value in profile.items():
            if key.endswith("_us"):
                stages[key.removesuffix("_us")] += value / 1000
            elif key != "event":
                counters[key] += value
    scenes = defaultdict(list)
    for row in graphics:
        key = (row["phase"], row["map"], row["mode"], row["cutscene"], row["scene_timer"] // 60)
        scenes[key].append(row)
    scene_groups = []
    for key, records in sorted(scenes.items()):
        if len(records) < 4:
            continue
        scene_groups.append({"key": key, "frames": len(records),
                             "graphics_ms": distribution([r["duration_us"] / 1000 for r in records]),
                             "interval_ms": distribution(intervals(records)),
                             "start_position": [records[0][v] for v in ("x", "y", "z")]})
    clock_settings = sorted({(r["a"], r["b"], r["c"], r["d"]) for r in grouped[4]})
    return {"run": metadata["run"], "scenario": metadata["scenario"],
            "graphics_tasks": len(graphics), "game_loop_iterations": len(games),
            "presentations": len(presents), "graphics_ms": distribution([r["duration_us"] / 1000 for r in graphics]),
            "presentation_requests": len(presents), "display_swap_calls": metadata.get("display_swap_calls"),
            "game_interval_ms": distribution(intervals(games)),
            "presentation_ms": distribution([r["duration_us"] / 1000 for r in presents]),
            "clocks_cpu_gpu_bus_xbar_mhz": clock_settings,
            "sampled_graphics_stage_ms": dict(sorted(stages.items(), key=lambda item: -item[1])),
            "sampled_graphics_counts": dict(counters), "scene_groups": scene_groups,
            "cpu_submission_is_not_gpu_execution_time": True}


def read_csv(path: Path) -> list[dict]:
    with path.open(newline="") as file:
        return [{key: float(value) if key in ("x", "y", "z") else int(value)
                 for key, value in row.items()} for row in csv.DictReader(file)]


def verify_lifecycle(output: Path, run_id: str) -> bool:
    lifecycle_path = output / "lifecycle.json"
    verified = False
    if lifecycle_path.exists():
        lifecycle = json.loads(lifecycle_path.read_text())
        if lifecycle.get("run") != run_id:
            raise ValueError("Shutdown receipt belongs to another run")
        if lifecycle.get("new_core_dumps"):
            raise ValueError("A new crash dump appeared during this benchmark")
        if lifecycle.get("shutdown_complete") is not True or lifecycle.get("inspection_error"):
            raise ValueError("Clean benchmark shutdown was not verified")
        verified = True
    return verified


def analyze(output: Path, run_id: str) -> dict:
    metadata = json.loads((output / (run_id + ".json")).read_text())
    if metadata.get("run") != run_id:
        raise ValueError("Completion receipt belongs to another run")
    verified = verify_lifecycle(output, run_id)
    result = summarize(read_csv(output / (run_id + ".csv")),
                       read_csv(output / (run_id + "-profile.csv")), metadata)
    result["clean_shutdown_verified"] = verified
    write_json(output / "summary.json", result)
    return result


def package_payload(path: Path) -> bytes:
    with zipfile.ZipFile(path) as package:
        if package.testzip() is not None:
            raise ValueError("Package CRC failure")
        payload = package.read("eboot.bin")
    if not 1024 <= len(payload) <= 32 * 1024 * 1024:
        raise ValueError("Unexpected executable size")
    return payload


def ensure_fresh_run(device, run_id: str) -> None:
    for suffix in ("-started.json", ".json", ".csv", "-profile.csv", "-shutdown.json"):
        path = DATA + "/" + run_id + suffix
        try:
            device.get(path, limit=64 * 1024 * 1024)
        except FileNotFoundError:
            continue
        raise ValueError("Remote run identity already exists; use a new run ID")


def wait_for_startup(device, output: Path, run_id: str, timeout: float = 20) -> None:
    # A successful URI request can leave another app in the foreground. Only
    # this run's own receipt establishes that the benchmark reached main().
    path = DATA + "/" + run_id + "-started.json"
    deadline = time.monotonic() + timeout
    while True:
        try:
            if device.exists(path):
                receipt = device.get(path, limit=65536)
                info = json.loads(receipt)
                if info.get("schema") != 1 or info.get("run") != run_id:
                    raise ValueError("Unexpected benchmark startup receipt")
                (output / (run_id + "-started.json")).write_bytes(receipt)
                print("benchmark startup verified", flush=True)
                return
        except FileNotFoundError:
            pass
        except ftplib.all_errors as error:
            print("startup probe:", str(error), flush=True)
        if time.monotonic() >= deadline:
            raise TimeoutError("DK64 did not reach verified startup; inspect the Vita's foreground screen before retrying")
        time.sleep(1)


def run(args) -> dict:
    if args.output.exists():
        raise FileExistsError(args.output)
    with Device(args.host) as device:
        before = device.core_inventory()
        try:
            run_on_device(args, device)
        finally:
            if args.output.exists():
                record_lifecycle(device, before, args.output, args.run_id)
        return analyze(args.output, args.run_id)


def record_lifecycle(device, before: dict, output: Path, run_id: str) -> dict:
    report = {"run": run_id, "shutdown_complete": False, "new_core_dumps": []}
    write_json(output / "core-dumps-before.json", before)
    try:
        after = device.core_inventory()
        write_json(output / "core-dumps-after.json", after)
        changed = [name for name, facts in after.items() if before.get(name) != facts]
        report["new_core_dumps"] = sorted(changed)
        for name in changed:
            directory = output / "coredumps"
            directory.mkdir(exist_ok=True)
            (directory / name).write_bytes(device.get(CORE_DATA + "/" + name, limit=256*1024*1024))
        shutdown_path = output / (run_id + "-shutdown.json")
        if shutdown_path.exists():
            shutdown = json.loads(shutdown_path.read_text())
            report["shutdown_complete"] = (shutdown.get("run") == run_id
                and shutdown.get("runtime_joined") is True and shutdown.get("exit_code") == 0)
        # These files belong to this run. Collect them after measurement and
        # restoration, including when startup succeeded but the capture stalled.
        for suffix in ("-startup.txt", "-watchdog.log", "-progress.bin"):
            try:
                trace = device.get(DATA + "/" + run_id + suffix, limit=1024*1024)
            except FileNotFoundError:
                continue
            (output / (run_id + suffix)).write_bytes(trace)
    except Exception as error:
        report["inspection_error"] = str(error)
        raise
    finally:
        write_json(output / "lifecycle.json", report)
    return report


def run_on_device(args, device) -> dict:
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,48}", args.run_id) or not 15 <= args.seconds <= 240 or not 0 <= args.profile_every <= 1024:
        raise ValueError("Invalid benchmark parameters")
    args.output.mkdir(parents=True, exist_ok=False)
    original, payload = package_payload(args.original), package_payload(args.package)
    print(device.command("version").strip(), flush=True)
    if device.get(EBOOT) != original:
        raise ValueError("Unexpected installed executable; inspect it before testing")
    before = {name: device.get(SAVES + "/" + name) for name in device.list(SAVES) if name not in (".", "..")}
    before_dir = args.output / "normal-saves-before"
    before_dir.mkdir()
    for name, value in before.items():
        (before_dir / name).write_bytes(value)
    manifest = {"run": args.run_id, "recorded_at": datetime.now(timezone.utc).isoformat(),
                "package_sha256": digest(args.package.read_bytes()), "eboot_sha256": digest(payload),
                "original_eboot_sha256": digest(original), "normal_saves_before": {n: digest(v) for n, v in before.items()}}
    write_json(args.output / "input.json", manifest)
    for name, repo in (("game", "."), ("rt64", "lib/rt64"), ("runtime", "lib/N64ModernRuntime")):
        (args.output / (name + "-revision.txt")).write_bytes(subprocess.check_output(["git", "-C", repo, "rev-parse", "HEAD"]))
        # Include staged edits as well. This audits the current checkout; the
        # immutable package hash still identifies the executable actually run.
        (args.output / (name + "-working.patch")).write_bytes(subprocess.check_output(["git", "-C", repo, "diff", "--binary", "HEAD"]))
    device.mkdir(DATA)
    ensure_fresh_run(device, args.run_id)
    device.mkdir(DATA + "/saves")
    rom = args.rom.read_bytes()
    if hashlib.sha1(rom).hexdigest() != "cf806ff2603640a748fca5026ded28802f1f4a50":
        raise ValueError("Benchmark requires the expected local US ROM")
    try:
        installed_rom = device.get(DATA + "/DK64.z64")
    except FileNotFoundError:
        installed_rom = None
    if installed_rom != rom:
        print("installing verified ROM in the private benchmark directory", flush=True)
        device.put(DATA + "/DK64.z64", rom)
    for name in ("DK64.bin", "DK64.bin.bak"):
        seed = (args.seed_directory / name).read_bytes() if args.seed_directory else before[name]
        device.put(DATA + "/saves/" + name, seed)
        (args.output / ("seed-" + name)).write_bytes(seed)
    configuration = (f"version=1\nrun={args.run_id}\nscenario={args.scenario}\nseconds={args.seconds}\nprofile_every={args.profile_every}\n").encode()
    device.put(DATA + "/run.cfg", configuration)
    (args.output / "run.cfg").write_bytes(configuration)
    # The fixture supplies input inside DK64. Even a global reset can finish a
    # pre-existing UI gesture, so the runner sends no controller commands.
    try:
        with temporary_executable(device, payload, original, args.output):
            device.launch()
            deadline = time.monotonic() + args.seconds + 90
            wait_for_startup(device, args.output, args.run_id)
            while time.monotonic() < deadline:
                try:
                    complete_ready = device.exists(DATA + "/" + args.run_id + ".json")
                except ftplib.all_errors as error:
                    print("result probe:", str(error), flush=True)
                    time.sleep(2)
                    continue
                if not complete_ready:
                    time.sleep(5)
                    continue
                try:
                    data = device.get(DATA + "/" + args.run_id + ".json", limit=65536)
                except FileNotFoundError:
                    time.sleep(5)
                    continue
                (args.output / (args.run_id + ".json")).write_bytes(data)
                for suffix in (".csv", "-profile.csv"):
                    (args.output / (args.run_id + suffix)).write_bytes(device.get(DATA + "/" + args.run_id + suffix))
                shutdown_deadline = min(deadline, time.monotonic() + 30)
                shutdown_path = DATA + "/" + args.run_id + "-shutdown.json"
                while not device.exists(shutdown_path):
                    if time.monotonic() >= shutdown_deadline:
                        raise TimeoutError("Capture completed, but runtime shutdown did not finish")
                    time.sleep(1)
                (args.output / (args.run_id + "-shutdown.json")).write_bytes(device.get(shutdown_path, limit=65536))
                break
            else:
                try:
                    (args.output / "failure.txt").write_bytes(device.get(DATA + "/failure.txt", limit=65536))
                except FileNotFoundError:
                    pass
                raise TimeoutError("The benchmark did not complete before its hard deadline")
    finally:
        after = {name: device.get(SAVES + "/" + name) for name in device.list(SAVES) if name not in (".", "..")}
        write_json(args.output / "save-verification.json", {"normal_saves_unchanged": before == after,
                   "after": {name: digest(value) for name, value in after.items()}})
        if before != after:
            raise RuntimeError("Normal save files changed during benchmarking")
    return {"run": args.run_id}


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="action", required=True)
    capture = sub.add_parser("run")
    capture.add_argument("--host", default="192.168.1.146")
    capture.add_argument("--package", type=Path, required=True)
    capture.add_argument("--original", type=Path, required=True)
    capture.add_argument("--rom", type=Path, required=True)
    capture.add_argument("--seed-directory", type=Path)
    capture.add_argument("--output", type=Path, required=True)
    capture.add_argument("--run-id", required=True)
    capture.add_argument("--scenario", choices=("attract", "world"), default="attract")
    capture.add_argument("--seconds", type=int, default=120)
    capture.add_argument("--profile-every", type=int, default=16)
    analysis = sub.add_parser("analyze")
    analysis.add_argument("output", type=Path)
    analysis.add_argument("run_id")
    recovery = sub.add_parser("recover")
    recovery.add_argument("output", type=Path)
    recovery.add_argument("--host", default="192.168.1.146")
    args = parser.parse_args()
    if args.action in ("run", "recover"):
        lock = Path("build/fps-autobench/device.lock")
        lock.parent.mkdir(parents=True, exist_ok=True)
        with lock.open("a") as handle:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            if args.action == "run":
                result = run(args)
            else:
                with Device(args.host) as device:
                    recover_executable(device, args.output)
                result = json.loads((args.output / "deployment.json").read_text())
    else:
        result = analyze(args.output, args.run_id)
    print(json.dumps({key: value for key, value in result.items() if key != "scene_groups"}, indent=2))


if __name__ == "__main__":
    main()
