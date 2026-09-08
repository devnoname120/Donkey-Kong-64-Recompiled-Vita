from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path

from vita_fps_benchmark import digest, distribution, read_csv, summarize, verify_lifecycle, write_json


def select(rows: list[dict], workload: str) -> tuple[list[dict], list[dict]]:
    if workload == "rap-crowd":
        selected = [r for r in rows if r["phase"] == 1 and r["map"] == 76
                    and r["mode"] == 2 and r["cutscene"] == 7 and 120 <= r["scene_timer"] < 660]
    elif workload == "world-idle":
        selected = [r for r in rows if r["phase"] == 2 and r["mode"] == 6
                    and r["mode_copy"] == 6 and not r["render"]
                    and not r["automatic"] and not r["paused"]]
    else:
        raise ValueError("Unknown comparison workload")
    game = sorted((r for r in selected if r["kind"] == 1), key=lambda r: r["time_us"])
    graphics = sorted((r for r in selected if r["kind"] == 2), key=lambda r: r["time_us"])
    if workload == "rap-crowd":
        expected = Counter(range(120, 660))
        if any(Counter(r["scene_timer"] for r in group) != expected for group in (game, graphics)):
            raise ValueError("Rap workload coverage is not exactly the selected timer window")
    else:
        if len(game) < 110 or len(graphics) < 110:
            raise ValueError("World idle workload coverage is incomplete")
        game, graphics = game[30:110], graphics[30:110]
    if any(r["a"] for r in graphics):
        raise ValueError("Sampled profiling events cannot be used for FPS comparisons")
    return game, graphics


def metrics(game: list[dict], graphics: list[dict]) -> dict:
    intervals = [(b["time_us"] - a["time_us"]) / 1000 for a, b in zip(game, game[1:])]
    if not intervals or min(intervals) <= 0:
        raise ValueError("Game timestamps are not strictly increasing")
    return {"game_frames": len(game), "graphics_tasks": len(graphics),
            "game_fps": (len(game) - 1) * 1000000 / (game[-1]["time_us"] - game[0]["time_us"]),
            "game_interval_ms": distribution(intervals),
            "graphics_ms": distribution([r["duration_us"] / 1000 for r in graphics]),
            "start_position": [game[0][key] for key in ("x", "y", "z")]}


def compare(reference_rows: list[dict], reference_metadata: dict,
            candidate_rows: list[dict], candidate_metadata: dict, workload: str) -> dict:
    for metadata in (reference_metadata, candidate_metadata):
        if metadata.get("architecture_probe"):
            raise ValueError("Architecture replay captures cannot be used for game FPS comparisons")
        if metadata.get("debug_watchdog"):
            raise ValueError("Debug watchdog captures cannot be used for FPS comparisons")
        if metadata.get("external_input_attempted"):
            raise ValueError("External input or screenshot attempts cannot be used for FPS comparisons")
        if metadata.get("profile_every") or metadata.get("profiles") or metadata.get("compiled_stage_profiling"):
            raise ValueError("Detailed profiling must be disabled for FPS comparisons")
    reference = summarize(reference_rows, [], reference_metadata)
    candidate = summarize(candidate_rows, [], candidate_metadata)
    clocks = reference["clocks_cpu_gpu_bus_xbar_mhz"]
    if len(clocks) != 1 or clocks != candidate["clocks_cpu_gpu_bus_xbar_mhz"]:
        raise ValueError("Actual clock settings differ or change during the compared runs")
    a_game, a_graphics = select(reference_rows, workload)
    b_game, b_graphics = select(candidate_rows, workload)
    if workload == "world-idle":
        maps = {r["map"] for r in a_game + b_game}
        if len(maps) != 1 or any(abs(a_game[0][key] - b_game[0][key]) > 0.001 for key in ("x", "y", "z")):
            raise ValueError("World idle location differs between runs")
    a, b = metrics(a_game, a_graphics), metrics(b_game, b_graphics)
    return {"workload": workload, "clocks_cpu_gpu_bus_xbar_mhz": clocks[0],
            "clean_shutdown_verified": all(m.get("clean_shutdown_verified") is True
                for m in (reference_metadata, candidate_metadata)),
            "reference_run": reference_metadata["run"], "candidate_run": candidate_metadata["run"],
            "reference": a, "candidate": b,
            "graphics_mean_reduction_percent": (a["graphics_ms"]["mean"] - b["graphics_ms"]["mean"]) / a["graphics_ms"]["mean"] * 100,
            "game_fps_improvement_percent": (b["game_fps"] - a["game_fps"]) / a["game_fps"] * 100,
            "note": "Game-loop throughput and CPU graphics-task latency; not presentation FPS or isolated GPU execution time."}


def load(path: Path) -> tuple[list[dict], dict]:
    manifest = json.loads((path / "input.json").read_text())
    run = manifest["run"]
    metadata = json.loads((path / (run + ".json")).read_text())
    if metadata.get("run") != run:
        raise ValueError("Run identity differs from its input manifest")
    metadata["external_input_attempted"] = bool(manifest.get("external_input_attempted"))
    metadata["clean_shutdown_verified"] = verify_lifecycle(path, run)
    if not metadata["clean_shutdown_verified"]:
        raise ValueError("Directory comparison requires verified benchmark shutdown")
    for filename, field in (("deployment.json", "restored"),
                            ("save-verification.json", "normal_saves_unchanged")):
        receipt_path = path / filename
        if not receipt_path.exists() or json.loads(receipt_path.read_text()).get(field) is not True:
            raise ValueError("Directory comparison requires verified executable restoration and save preservation")
    return read_csv(path / (run + ".csv")), metadata


def compare_directories(reference: Path, candidate: Path, workload: str) -> dict:
    for name in ("seed-DK64.bin", "seed-DK64.bin.bak"):
        if digest((reference / name).read_bytes()) != digest((candidate / name).read_bytes()):
            raise ValueError("Starting save snapshots differ")
    return compare(*load(reference), *load(candidate), workload)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--workload", choices=("rap-crowd", "world-idle"), required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = compare_directories(args.reference, args.candidate, args.workload)
    if args.output:
        write_json(args.output, result)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
