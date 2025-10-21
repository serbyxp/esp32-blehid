import ctypes
import subprocess
import tempfile
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
MAIN_DIR = PROJECT_ROOT / "main"


class KeyboardState(ctypes.Structure):
    _fields_ = [
        ("modifiers", ctypes.c_uint8),
        ("reserved", ctypes.c_uint8),
        ("keys", ctypes.c_uint8 * 6),
    ]


class WsAsciiSequenceTest(unittest.TestCase):
    MAX_REPORTS_PER_CHAR = 4

    @classmethod
    def setUpClass(cls) -> None:
        cls._lib = cls._build_test_library()
        cls._lib.ws_ascii_build_sequence.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.POINTER(KeyboardState),
            ctypes.c_size_t,
        ]
        cls._lib.ws_ascii_build_sequence.restype = ctypes.c_size_t
        cls._lib.ws_ascii_build_char.argtypes = [
            ctypes.c_uint8,
            ctypes.POINTER(KeyboardState),
            ctypes.c_size_t,
        ]
        cls._lib.ws_ascii_build_char.restype = ctypes.c_size_t

    @staticmethod
    def _build_test_library() -> ctypes.CDLL:
        with tempfile.TemporaryDirectory() as tmpdir:
            library_path = Path(tmpdir) / "libws_ascii.so"
            compile_cmd = [
                "gcc",
                "-std=c11",
                "-shared",
                "-fPIC",
                "-DUNIT_TEST",
                "-I",
                str(MAIN_DIR),
                str(MAIN_DIR / "ws_ascii.c"),
                str(MAIN_DIR / "hid_keymap.c"),
                "-o",
                str(library_path),
            ]
            subprocess.check_call(compile_cmd, cwd=PROJECT_ROOT)
            return ctypes.CDLL(str(library_path))

    def _build_char_reports(self, ch: str) -> list[KeyboardState]:
        ascii_value = ord(ch)
        state_array_type = KeyboardState * self.MAX_REPORTS_PER_CHAR
        state_buffer = state_array_type()
        count = self._lib.ws_ascii_build_char(
            ctypes.c_uint8(ascii_value),
            state_buffer,
            self.MAX_REPORTS_PER_CHAR,
        )
        return [state_buffer[i] for i in range(count)]

    def _build_sequence(self, text: str) -> list[KeyboardState]:
        reports: list[KeyboardState] = []
        for ch in text:
            reports.extend(self._build_char_reports(ch))
        return reports

    def test_mixed_case_string_releases_modifiers_before_digits(self) -> None:
        text = "Hello from ESP32!"
        char_reports = [self._build_char_reports(ch) for ch in text]
        flattened = [state for reports in char_reports for state in reports]

        actual_modifiers = [
            [state.modifiers for state in reports] for reports in char_reports
        ]

        expected_modifiers: list[list[int]] = []
        for ch in text:
            if (ch.isalpha() and ch.isupper()) or ch in {"!"}:
                expected_modifiers.append([0x02, 0x02, 0x02, 0x00])
            else:
                expected_modifiers.append([0x00, 0x00])

        self.assertEqual(
            actual_modifiers,
            expected_modifiers,
            msg="Modifier transitions should follow the modifier press, combo, key release, and modifier release cadence.",
        )

        for ch, reports in zip(text, char_reports):
            if (ch.isalpha() and ch.isupper()) or ch in {"!"}:
                self.assertTrue(all(key == 0 for key in reports[0].keys))
                self.assertNotEqual(reports[1].keys[0], 0)
                self.assertTrue(all(key == 0 for key in reports[2].keys))
                self.assertTrue(all(key == 0 for key in reports[3].keys))

        offsets: list[int] = []
        total = 0
        for reports in char_reports:
            offsets.append(total)
            total += len(reports)

        for index, ch in enumerate(text):
            if ch.isdigit():
                start = offsets[index]
                if start > 0:
                    self.assertEqual(
                        flattened[start - 1].modifiers,
                        0x00,
                        msg=f"Digit {ch!r} should follow a cleared modifier report",
                    )

                for report in char_reports[index]:
                    self.assertEqual(
                        report.modifiers,
                        0x00,
                        msg=f"Digit {ch!r} should not carry modifiers",
                    )

        encoded = text.encode("ascii")
        text_type = ctypes.c_uint8 * len(encoded)
        text_buffer = text_type(*encoded)
        capacity = len(text) * self.MAX_REPORTS_PER_CHAR
        state_array_type = KeyboardState * capacity
        state_buffer = state_array_type()
        count = self._lib.ws_ascii_build_sequence(
            text_buffer,
            len(encoded),
            state_buffer,
            capacity,
        )
        self.assertEqual(count, len(flattened))
        for idx in range(count):
            expected = flattened[idx]
            observed = state_buffer[idx]
            self.assertEqual(observed.modifiers, expected.modifiers)
            self.assertEqual(list(observed.keys), list(expected.keys))


if __name__ == "__main__":
    unittest.main()
