import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from verify_vita_camera import capture, restart


class CameraReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.capture_dir = self.make_run("capture", b"before", b"after", False)
        self.restart_dir = self.make_run("restart", b"after", b"after reload", True)
        self.log = (
            "Camera probe prerequisites: camera=1 film=10 fairy_flag=0\n"
            "Game framebuffer readback 2: address=0004fa00 requested=153600 returned=153600 written=153600 graphics_tasks=1500\n"
            "Camera probe picture: index=0 actor=80400000 pixels=80200000 result=0\n"
            "Game framebuffer readback 3: address=0002a200 requested=153600 returned=153600 written=153600 graphics_tasks=1600\n"
            "Camera probe picture: index=1 actor=80400000 pixels=80200000 result=1\n"
            "Camera probe frame=100 phase=9 state=131 result=255 film=8 busy=0 captured=1 picture=00000000 queued=3 freed=3\n"
            "Camera probe frame=101 phase=9 state=12 result=255 film=11 busy=0 captured=1 picture=00000000 queued=3 freed=3\n"
            "Camera probe completed: photos=2 film=11 fairy_flag=1 queued=3 freed=3\n"
        )
        (self.capture_dir / "progress.log").write_text(self.log)
        (self.restart_dir / "progress.log").write_text(
            "Camera probe save verification passed: camera=1 fairy_flag=1 film=11; no prerequisites changed\n")
        for name in ("readback-2-0004fa00.bin", "readback-3-0002a200.bin"):
            (self.capture_dir / name).write_bytes(b"\x00\x01" * 76800)
        for i in range(2):
            (self.capture_dir / f"camera-photo-{i}.rgba16").write_bytes(b"\x18\xc1" * 20480)

    def make_run(self, name, initial, saved, verify):
        directory = self.root / name
        (directory / "saves-after").mkdir(parents=True)
        (directory / "saves-after" / "DK64.bin").write_bytes(saved)
        (directory / "DK64AdventureProbe.vpk").write_bytes(b"test archive")
        info = {"cpu_opt": False, "verify_save": verify,
                "vpk_sha256": hashlib.sha256(b"test archive").hexdigest(),
                "saves": {"DK64.bin": hashlib.sha256(initial).hexdigest()}}
        (directory / "input.json").write_text(json.dumps(info))
        (directory / "process.json").write_text(json.dumps({"exit_code": 124}))
        (directory / "config.yml").write_text(
            "backend-renderer: Vulkan\nmemory-mapping: external-host\ncpu-opt: false\n")
        return directory

    def test_capture_and_restart(self):
        receipt = capture(self.capture_dir)
        self.assertEqual([photo["matches"] for photo in receipt["photographs"]], [20480, 20480])
        self.assertEqual(receipt["emulator_exit_code"], 124)
        self.assertTrue(restart(self.restart_dir, receipt)["verified"])

    def test_rejects_completion_during_reward(self):
        (self.capture_dir / "progress.log").write_text(self.log.replace("state=12", "state=131"))
        with self.assertRaisesRegex(ValueError, "normal control"):
            capture(self.capture_dir)

    def test_rejects_missing_completion(self):
        (self.capture_dir / "progress.log").write_text(self.log.split("Camera probe completed:")[0])
        with self.assertRaisesRegex(ValueError, "did not complete"):
            capture(self.capture_dir)

    def test_rejects_incorrect_pixels(self):
        (self.capture_dir / "camera-photo-1.rgba16").write_bytes(b"\x00\x00" * 20480)
        with self.assertRaisesRegex(ValueError, "incorrect pixels"):
            capture(self.capture_dir)

    def test_rejects_reused_readback(self):
        lines = [line for line in self.log.splitlines() if not line.startswith("Game framebuffer readback 3:")]
        (self.capture_dir / "progress.log").write_text("\n".join(lines))
        with self.assertRaisesRegex(ValueError, "ordered source"):
            capture(self.capture_dir)

    def test_rejects_changed_restart_inputs(self):
        manifest_path = self.restart_dir / "input.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["saves"]["DK64.bin"] = hashlib.sha256(b"unrelated").hexdigest()
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "different save"):
            restart(self.restart_dir, capture(self.capture_dir))

    def test_rejects_restart_prerequisite_mutations(self):
        with (self.restart_dir / "progress.log").open("a") as log:
            log.write("Camera probe prerequisites: camera=1 film=10 fairy_flag=0\n")
        with self.assertRaisesRegex(ValueError, "prerequisites"):
            restart(self.restart_dir, capture(self.capture_dir))

    def test_rejects_changed_archive(self):
        (self.capture_dir / "DK64AdventureProbe.vpk").write_bytes(b"other archive")
        with self.assertRaisesRegex(ValueError, "differs"):
            capture(self.capture_dir)


if __name__ == "__main__":
    unittest.main()
