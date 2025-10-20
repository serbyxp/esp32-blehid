import ctypes
import ctypes
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
MAIN_DIR = PROJECT_ROOT / "main"
HID_MOUSE_REPORT_LEN = 6


class MouseState(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("x", ctypes.c_int8),
        ("y", ctypes.c_int8),
        ("wheel", ctypes.c_int8),
        ("hwheel", ctypes.c_int8),
        ("buttons", ctypes.c_uint8),
    ]


class WsMouseReportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._lib = cls._build_test_library()
        cls._lib.mouse_build_report.argtypes = [
            ctypes.POINTER(MouseState),
            ctypes.POINTER(ctypes.c_uint8),
        ]
        cls._lib.mouse_build_report.restype = None

    @staticmethod
    def _build_test_library() -> ctypes.CDLL:
        with tempfile.TemporaryDirectory() as tmpdir:
            library_path = Path(tmpdir) / "libmouse_report.so"
            compile_cmd = [
                "gcc",
                "-std=c11",
                "-shared",
                "-fPIC",
                "-I",
                str(PROJECT_ROOT / "tests" / "stubs"),
                "-I",
                str(MAIN_DIR),
                str(MAIN_DIR / "mouse_report_builder.c"),
                "-o",
                str(library_path),
            ]
            subprocess.check_call(compile_cmd, cwd=PROJECT_ROOT)
            return ctypes.CDLL(str(library_path))

    def _build_state_from_json(self, payload: str) -> MouseState:
        message = json.loads(payload)
        if message.get("type") != "mouse":
            raise AssertionError("Message must describe a mouse update")

        state = MouseState()
        state.x = message.get("dx", 0)
        state.y = message.get("dy", 0)
        state.wheel = message.get("wheel", 0)
        state.hwheel = message.get("hwheel", 0)

        buttons = message.get("buttons", {}) or {}
        state.buttons = 0
        if buttons.get("left"):
            state.buttons |= 0x01
        if buttons.get("right"):
            state.buttons |= 0x02
        if buttons.get("middle"):
            state.buttons |= 0x04
        if buttons.get("back"):
            state.buttons |= 0x08
        if buttons.get("forward"):
            state.buttons |= 0x10

        return state

    def _report_for(self, payload: str) -> list[int]:
        state = self._build_state_from_json(payload)
        report_buffer_type = ctypes.c_uint8 * HID_MOUSE_REPORT_LEN
        report_buffer = report_buffer_type()
        self._lib.mouse_build_report(ctypes.byref(state), report_buffer)
        return list(report_buffer)

    def test_pure_x_delta_updates_second_axis_byte(self) -> None:
        report = self._report_for('{"type":"mouse","dx":42}')
        self.assertEqual(report, [1, 0x00, 42, 0, 0, 0])

    def test_pure_y_delta_updates_third_axis_byte(self) -> None:
        report = self._report_for('{"type":"mouse","dy":-7}')
        # Two's complement for -7 is 0xF9
        self.assertEqual(report, [1, 0x00, 0, 0xF9, 0, 0])

    def test_vertical_scroll_populates_wheel_byte(self) -> None:
        report = self._report_for('{"type":"mouse","wheel":3}')
        self.assertEqual(report, [1, 0x00, 0, 0, 3, 0])

    def test_horizontal_scroll_populates_hwheel_byte(self) -> None:
        report = self._report_for('{"type":"mouse","hwheel":-4}')
        # -4 encoded as two's complement 0xFC
        self.assertEqual(report, [1, 0x00, 0, 0, 0, 0xFC])

    def test_button_combination_sets_expected_bitfield(self) -> None:
        report = self._report_for(
            '{"type":"mouse","buttons":{"left":true,"back":true,"forward":true}}'
        )
        self.assertEqual(report, [1, 0x19, 0, 0, 0, 0])


if __name__ == "__main__":
    unittest.main()
