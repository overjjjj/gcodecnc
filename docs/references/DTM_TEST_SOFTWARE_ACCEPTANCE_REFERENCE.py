import json
import os
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "bin" / "DTMDesignTest.exe"


class DesignInterfaceAcceptanceTests(unittest.TestCase):
    def run_app(self, *args: str) -> subprocess.CompletedProcess[str]:
        self.assertTrue(EXE.exists(), f"test executable is missing: {EXE}")
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        return subprocess.run(
            [str(EXE), *args],
            cwd=EXE.parent,
            env=environment,
            text=True,
            encoding="utf-8",
            capture_output=True,
            timeout=30,
            check=False,
        )

    def test_reports_recovered_ribbon_groups(self) -> None:
        result = self.run_app("--describe")
        self.assertEqual(result.returncode, 0, result.stderr)
        state = json.loads(result.stdout)
        self.assertEqual(
            state["ribbonGroups"],
            ["文件&零件", "孔类", "牙类", "铣面", "槽类", "外形类", "斜面&圆角", "模块", "辅助", "发送"],
        )
        self.assertEqual(state["layout"], ["任务面板", "三维视图区", "属性", "工序树"])

    def test_add_operation_updates_tree_and_dirty_state(self) -> None:
        result = self.run_app("--scenario", "add-operation")
        self.assertEqual(result.returncode, 0, result.stderr)
        state = json.loads(result.stdout)
        self.assertTrue(state["dirty"])
        self.assertEqual(state["operationCount"], 1)
        self.assertEqual(state["operations"][0]["name"], "钻 G81")
        self.assertEqual(state["operations"][0]["status"], "待生成")

    def test_auto_hole_creates_batch_operations(self) -> None:
        result = self.run_app("--scenario", "auto-hole")
        self.assertEqual(result.returncode, 0, result.stderr)
        state = json.loads(result.stdout)
        self.assertEqual(state["operationCount"], 3)
        self.assertEqual([item["diameter"] for item in state["operations"]], [6.0, 8.0, 10.0])
        self.assertTrue(all(item["source"] == "自动孔" for item in state["operations"]))

    def test_rejects_step_over_larger_than_tool_diameter(self) -> None:
        result = self.run_app("--scenario", "invalid-parameters")
        self.assertEqual(result.returncode, 2, result.stderr)
        state = json.loads(result.stdout)
        self.assertFalse(state["valid"])
        self.assertEqual(state["error"], "切削步距不能大于刀具直径")

    def test_renders_design_window_to_png(self) -> None:
        temporary_root = ROOT / "tests" / ".tmp"
        temporary_root.mkdir(exist_ok=True)
        screenshot = temporary_root / "design-window.png"
        screenshot.unlink(missing_ok=True)
        result = self.run_app("--render", str(screenshot))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(screenshot.exists())
        self.assertGreater(screenshot.stat().st_size, 10_000)
        self.assertEqual(screenshot.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")


if __name__ == "__main__":
    unittest.main()
