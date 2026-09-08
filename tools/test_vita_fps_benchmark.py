import json
import tempfile
import unittest
from unittest.mock import Mock, patch
from pathlib import Path
from types import SimpleNamespace

import vita_fps_benchmark as bench


class FakeDevice:
    def __init__(self):
        self.files = {bench.EBOOT: b"normal"}
        self.commands = []

    def get(self, path, limit=64 * 1024 * 1024):
        if path not in self.files:
            raise FileNotFoundError(path)
        return self.files[path]

    def put(self, path, data):
        self.files[path] = data

    def rename(self, source, target):
        if target in self.files:
            raise FileExistsError(target)
        self.files[target] = self.files.pop(source)

    def kill(self):
        self.commands.append("kill")


class BenchmarkTests(unittest.TestCase):
    def test_corrupt_upload_never_replaces_the_installed_executable(self):
        class CorruptDevice(FakeDevice):
            def put(self, path, data):
                super().put(path, data[:-1])
        device = CorruptDevice()
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(RuntimeError, "Staged benchmark"):
                with bench.temporary_executable(device, b"benchmark", b"normal", Path(temporary)):
                    self.fail("Corrupt benchmark reached launch")
            self.assertEqual(device.get(bench.EBOOT), b"normal")
            self.assertEqual(device.commands, [])

    def test_failed_capture_verifies_saves_without_restoring_an_old_save(self):
        for changed in (False, True):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                device = FakeDevice()
                device.files.update({bench.SAVES + "/DK64.bin": b"save",
                                     bench.SAVES + "/DK64.bin.bak": b"backup",
                                     bench.DATA + "/DK64.z64": b"rom"})
                device.mkdir = Mock()
                device.list = Mock(return_value=["DK64.bin", "DK64.bin.bak"])
                device.command = Mock(return_value="version")
                def launch():
                    if changed:
                        device.files[bench.SAVES + "/DK64.bin"] = b"later save"
                device.launch = launch
                root = Path(temporary)
                for name, data in (("original", b"normal"), ("package", b"benchmark"), ("rom", b"rom")):
                    (root / name).write_bytes(data)
                args = SimpleNamespace(run_id="interrupted", seconds=120, profile_every=0,
                    output=root / "run", original=root / "original", package=root / "package",
                    rom=root / "rom", seed_directory=None, scenario="world")
                with patch.object(bench, "package_payload", side_effect=lambda path: path.read_bytes()), \
                        patch.object(bench, "ensure_fresh_run"), \
                        patch.object(bench, "wait_for_startup", side_effect=TimeoutError("capture interrupted")), \
                        patch.object(bench.hashlib, "sha1") as sha1, \
                        patch.object(bench.subprocess, "check_output", return_value=b"source snapshot"):
                    sha1.return_value.hexdigest.return_value = "cf806ff2603640a748fca5026ded28802f1f4a50"
                    with self.assertRaisesRegex(RuntimeError if changed else TimeoutError,
                                                "Normal save" if changed else "capture interrupted"):
                        bench.run_on_device(args, device)
                self.assertEqual(device.get(bench.EBOOT), b"normal")
                self.assertEqual(device.get(bench.SAVES + "/DK64.bin"), b"later save" if changed else b"save")
                receipt = json.loads((args.output / "save-verification.json").read_text())
                self.assertEqual(receipt["normal_saves_unchanged"], not changed)

    def test_complete_run_does_not_send_global_controller_input(self):
        class IsolatedDevice(FakeDevice):
            def __init__(self):
                super().__init__()
                self.files.update({bench.SAVES + "/DK64.bin": b"save",
                                   bench.SAVES + "/DK64.bin.bak": b"backup",
                                   bench.DATA + "/DK64.z64": b"rom"})
            def command(self, value):
                self.commands.append(value)
                if value.startswith(("press ", "release ")):
                    raise AssertionError("Global controller input could act on LiveArea")
                return "vitacompanion 1.06\n"
            def mkdir(self, path):
                pass
            def list(self, path):
                return ["DK64.bin", "DK64.bin.bak"]
            def exists(self, path):
                return path in self.files
            def launch(self):
                self.commands.append("launch " + bench.TITLE)
                prefix = bench.DATA + "/isolated-run"
                self.files.update({prefix + "-started.json": b'{"schema":1,"run":"isolated-run"}',
                                   prefix + ".json": b'{"run":"isolated-run"}',
                                   prefix + ".csv": b"events", prefix + "-profile.csv": b"profiles",
                                   prefix + "-shutdown.json": b'{"run":"isolated-run","runtime_joined":true,"exit_code":0}'})
        device = IsolatedDevice()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, data in (("original", b"normal"), ("package", b"benchmark"), ("rom", b"rom")):
                (root / name).write_bytes(data)
            args = SimpleNamespace(run_id="isolated-run", seconds=120, profile_every=0,
                                   output=root / "run", original=root / "original", package=root / "package",
                                   rom=root / "rom", seed_directory=None, scenario="attract")
            with patch.object(bench, "package_payload", side_effect=lambda path: path.read_bytes()), \
                    patch.object(bench.hashlib, "sha1") as sha1, \
                    patch.object(bench.subprocess, "check_output", return_value=b"source snapshot"):
                sha1.return_value.hexdigest.return_value = "cf806ff2603640a748fca5026ded28802f1f4a50"
                self.assertEqual(bench.run_on_device(args, device), {"run": "isolated-run"})
            self.assertEqual(device.get(bench.EBOOT), b"normal")
            self.assertTrue(json.loads((args.output / "save-verification.json").read_text())["normal_saves_unchanged"])

    def test_no_start_receipt_times_out_without_navigation(self):
        device = Mock()
        device.exists.return_value = False
        clock = [0.0]
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(bench.time, "monotonic", side_effect=lambda: clock[0]), \
                patch.object(bench.time, "sleep", side_effect=lambda delay: clock.__setitem__(0, clock[0] + delay)):
            with self.assertRaisesRegex(TimeoutError, "foreground screen"):
                bench.wait_for_startup(device, Path(temporary), "new-run", timeout=3)
            self.assertEqual(list(Path(temporary).iterdir()), [])
        device.command.assert_not_called()
        device.get.assert_not_called()
        self.assertEqual(clock[0], 3)

    def test_startup_requires_the_fresh_run_identity(self):
        device = Mock()
        device.exists.return_value = True
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            device.get.return_value = b'{"schema":1,"run":"old-run"}'
            with self.assertRaisesRegex(ValueError, "startup receipt"):
                bench.wait_for_startup(device, output, "new-run")
            self.assertEqual(list(output.iterdir()), [])
            device.get.return_value = b'{"schema":1,"run":"new-run"}'
            bench.wait_for_startup(device, output, "new-run")
            self.assertEqual((output / "new-run-started.json").read_bytes(), device.get.return_value)
        device.command.assert_not_called()

    def test_recovery_preserves_an_existing_retirement_path(self):
        device = FakeDevice()
        device.files = {bench.EBOOT: b"benchmark", "backup": b"normal", "retired": b"preserved"}
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            bench.write_json(output / "deployment.json", {
                "original_sha256": bench.digest(b"normal"),
                "benchmark_sha256": bench.digest(b"benchmark"),
                "backup": "backup", "retired": "retired"})
            with self.assertRaisesRegex(ValueError, "retirement path"):
                bench.recover_executable(device, output)
            self.assertEqual(device.get("retired"), b"preserved")
            self.assertEqual(device.get(bench.EBOOT), b"benchmark")
            self.assertEqual(device.commands, [])

    def test_missing_result_probe_does_not_open_a_data_transfer(self):
        transport = Mock()
        transport.size.side_effect = bench.ftplib.error_perm("550 Not found")
        with patch.object(bench.ftplib, "FTP", return_value=transport):
            with bench.Device("device") as device:
                self.assertFalse(device.exists("/pending.json"))
            transport.retrbinary.assert_not_called()

    def test_refuses_reused_remote_run_identity(self):
        device = FakeDevice()
        device.files[bench.DATA + "/used-started.json"] = b"{}"
        with self.assertRaisesRegex(ValueError, "already exists"):
            bench.ensure_fresh_run(device, "used")
        bench.ensure_fresh_run(device, "new")

    def test_recovers_interrupted_verified_replacement(self):
        device = FakeDevice()
        device.files = {bench.EBOOT: b"benchmark", "backup": b"normal"}
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            receipt = {"original_sha256": bench.digest(b"normal"),
                       "benchmark_sha256": bench.digest(b"benchmark"),
                       "backup": "backup", "retired": "retired", "installed": True,
                       "restored": False}
            bench.write_json(output / "deployment.json", receipt)
            bench.recover_executable(device, output)
            self.assertEqual(device.get(bench.EBOOT), b"normal")
            self.assertEqual(device.get("retired"), b"benchmark")
            self.assertTrue(json.loads((output / "deployment.json").read_text())["restored"])
            device.commands.clear()
            bench.recover_executable(device, output)
            self.assertEqual(device.commands, [])

    def test_recovery_refuses_unrecognized_executable(self):
        device = FakeDevice()
        device.files = {bench.EBOOT: b"external", "backup": b"normal"}
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            bench.write_json(output / "deployment.json", {
                "original_sha256": bench.digest(b"normal"),
                "benchmark_sha256": bench.digest(b"benchmark"),
                "backup": "backup", "retired": "retired"})
            with self.assertRaisesRegex(ValueError, "Unrecognized"):
                bench.recover_executable(device, output)
            self.assertEqual(device.files, {bench.EBOOT: b"external", "backup": b"normal"})
            self.assertEqual(device.commands, [])

    def test_ftp_reuses_connection_and_sends_quit(self):
        transport = Mock()
        transport.retrbinary.side_effect = lambda command, receive, **kwargs: receive(b"value")
        with patch.object(bench.ftplib, "FTP", return_value=transport) as factory:
            with bench.Device("device") as device:
                self.assertEqual(device.get("/one"), b"value")
                self.assertEqual(device.get("/two"), b"value")
            factory.assert_called_once()
            transport.quit.assert_called_once()
            transport.close.assert_called_once()

    def test_ftp_read_retries_a_disconnected_transport(self):
        first, second = Mock(), Mock()
        first.retrbinary.side_effect = TimeoutError("interrupted read")
        second.retrbinary.side_effect = lambda command, receive, **kwargs: receive(b"complete")
        with patch.object(bench.ftplib, "FTP", side_effect=[first, second]), patch.object(bench.time, "sleep"):
            with bench.Device("device") as device:
                self.assertEqual(device.get("/one"), b"complete")
            first.close.assert_called_once()
            second.quit.assert_called_once()

    def test_launch_wakes_display_without_synthetic_navigation(self):
        class Control(bench.Device):
            def __init__(self):
                self.commands = []
            def command(self, value):
                self.commands.append(value)
                return "Turning display on.\n" if value == "screen on" else "Launched.\n"
        device = Control()
        device.launch()
        self.assertEqual(device.commands, ["screen on", "launch " + bench.TITLE])

    def test_restore_after_failed_run(self):
        device = FakeDevice()
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(RuntimeError, "fixture failed"):
                with bench.temporary_executable(device, b"benchmark", b"normal", Path(temporary)):
                    self.assertEqual(device.get(bench.EBOOT), b"benchmark")
                    raise RuntimeError("fixture failed")
            self.assertEqual(device.get(bench.EBOOT), b"normal")
            receipt = json.loads((Path(temporary) / "deployment.json").read_text())
            self.assertTrue(receipt["restored"])

    def test_refuses_unexpected_installed_binary(self):
        device = FakeDevice()
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "installed executable"):
                with bench.temporary_executable(device, b"benchmark", b"different", Path(temporary)):
                    self.fail("unverified installation was replaced")
            self.assertEqual(device.files, {bench.EBOOT: b"normal"})
            self.assertEqual(device.commands, [])

    def test_does_not_overwrite_another_edit_during_run(self):
        device = FakeDevice()
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(RuntimeError, "changed externally"):
                with bench.temporary_executable(device, b"benchmark", b"normal", Path(temporary)):
                    device.files[bench.EBOOT] = b"external"
            self.assertEqual(device.get(bench.EBOOT), b"external")
            self.assertIn(b"normal", device.files.values())

    def test_rejects_incomplete_or_truncated_capture(self):
        metadata = {"schema": 1, "run": "case", "scenario": "attract", "complete": True,
                    "valid": True, "events": 1, "profiles": 0, "dropped": 0,
                    "dropped_profiles": 0}
        with self.assertRaisesRegex(ValueError, "event count"):
            bench.summarize([], [], metadata)
        metadata["events"] = 0
        metadata["complete"] = False
        with self.assertRaisesRegex(ValueError, "complete"):
            bench.summarize([], [], metadata)

    def test_rates_do_not_equate_presentations_and_frames(self):
        rows = []
        base = {"duration_us": 0, "a": 0, "b": 0, "c": 0, "d": 0, "phase": 1,
                "map": 76, "mode": 3, "mode_copy": 3, "render": 1, "cutscene": 0,
                "scene_timer": 0, "game_frame": 0, "lag": 2, "automatic": 0,
                "paused": 0, "x": 0.0, "y": 0.0, "z": 0.0}
        for index in range(60):
            rows.append(dict(base, time_us=index * 16667, kind=3, sequence=index + 1))
            if index % 2 == 0:
                rows.append(dict(base, time_us=index * 16667, kind=1, sequence=index // 2 + 1))
                rows.append(dict(base, time_us=index * 16667, kind=2, sequence=index // 2 + 1, duration_us=10000))
        metadata = {"schema": 1, "run": "case", "scenario": "attract", "complete": True,
                    "valid": True, "events": len(rows), "profiles": 0, "dropped": 0,
                    "dropped_profiles": 0}
        result = bench.summarize(rows, [], metadata)
        self.assertEqual(result["graphics_tasks"], 30)
        self.assertEqual(result["game_loop_iterations"], 30)
        self.assertEqual(result["presentations"], 60)
        metadata["display_swap_calls"] = 30
        result = bench.summarize(rows, [], metadata)
        self.assertEqual(result["presentation_requests"], 60)
        self.assertEqual(result["display_swap_calls"], 30)
        self.assertAlmostEqual(result["game_interval_ms"]["median"], 33.334, places=3)
        self.assertEqual(result["graphics_ms"]["median"], 10.0)

    def test_analysis_rejects_post_capture_crash(self):
        base = {"duration_us": 10, "a": 0, "b": 0, "c": 0, "d": 0, "phase": 1,
                "map": 76, "mode": 2, "cutscene": 7, "scene_timer": 120,
                "game_frame": 1, "x": 0, "y": 0, "z": 0}
        rows = [dict(base, kind=kind, time_us=i*40000, sequence=i+1)
                for i in range(10) for kind in (1, 2)]
        metadata = {"schema": 1, "run": "capture", "scenario": "attract", "complete": True,
                    "valid": True, "events": len(rows), "profiles": 0, "dropped": 0,
                    "dropped_profiles": 0}
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            bench.write_json(output / "capture.json", metadata)
            bench.write_json(output / "lifecycle.json", {"run": "capture", "shutdown_complete": True,
                "new_core_dumps": ["psp2core-new-eboot.bin.psp2dmp"]})
            with patch.object(bench, "read_csv", side_effect=[rows, []]):
                with self.assertRaisesRegex(ValueError, "crash dump"):
                    bench.analyze(output, "capture")

    def test_analysis_rejects_incomplete_shutdown(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            bench.write_json(output / "capture.json", {"run": "capture"})
            bench.write_json(output / "lifecycle.json", {"run": "capture", "shutdown_complete": False,
                "new_core_dumps": []})
            with patch.object(bench, "read_csv", return_value=[]), patch.object(bench, "summarize", return_value={}):
                with self.assertRaisesRegex(ValueError, "shutdown"):
                    bench.analyze(output, "capture")

    def test_lifecycle_preserves_new_dump_and_requires_matching_shutdown(self):
        name = "psp2core-new-eboot.bin.psp2dmp"
        original = {"psp2core-old-eboot.bin.psp2dmp": {"type": "file", "size": "3"}}
        class CoreDevice(FakeDevice):
            def core_inventory(self):
                return dict(original, **{name: {"type": "file", "size": "4"}})
        device = CoreDevice()
        device.files[bench.CORE_DATA + "/" + name] = b"core"
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            bench.write_json(output / "capture-shutdown.json", {"run": "other",
                "runtime_joined": True, "exit_code": 0})
            result = bench.record_lifecycle(device, original, output, "capture")
            self.assertEqual(result["new_core_dumps"], [name])
            self.assertFalse(result["shutdown_complete"])
            self.assertEqual((output / "coredumps" / name).read_bytes(), b"core")
            self.assertEqual(device.get(bench.CORE_DATA + "/" + name), b"core")
            self.assertEqual(device.commands, [])

    def test_lifecycle_clean_capture(self):
        class CoreDevice(FakeDevice):
            def core_inventory(self):
                return {}
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            bench.write_json(output / "capture-shutdown.json", {"schema": 1, "run": "capture",
                "runtime_joined": True, "exit_code": 0})
            result = bench.record_lifecycle(CoreDevice(), {}, output, "capture")
            self.assertTrue(result["shutdown_complete"])
            self.assertEqual(result["new_core_dumps"], [])

    def test_incomplete_run_retains_only_its_own_progress_logs(self):
        class CoreDevice(FakeDevice):
            def core_inventory(self):
                return {}
        device = CoreDevice()
        device.files.update({bench.DATA + "/capture-startup.txt": b"game initialized",
                             bench.DATA + "/capture-watchdog.log": b"poll=21",
                             bench.DATA + "/old-startup.txt": b"stale"})
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            result = bench.record_lifecycle(device, {}, output, "capture")
            self.assertFalse(result["shutdown_complete"])
            self.assertEqual((output / "capture-startup.txt").read_bytes(), b"game initialized")
            self.assertEqual((output / "capture-watchdog.log").read_bytes(), b"poll=21")
            self.assertFalse((output / "old-startup.txt").exists())
            self.assertEqual(device.commands, [])

    def test_lifecycle_inspection_failure_is_not_clean(self):
        class OfflineDevice(FakeDevice):
            def core_inventory(self):
                raise TimeoutError("offline")
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            with self.assertRaises(TimeoutError):
                bench.record_lifecycle(OfflineDevice(), {}, output, "capture")
            result = json.loads((output / "lifecycle.json").read_text())
            self.assertFalse(result["shutdown_complete"])
            self.assertEqual(result["inspection_error"], "offline")


if __name__ == "__main__":
    unittest.main()
