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

    def _build_sequence(self, text: str) -> list[int]:
        encoded = text.encode("ascii")
        text_type = ctypes.c_uint8 * len(encoded)
        text_buffer = text_type(*encoded)
        capacity = len(encoded) * 2
        state_array_type = KeyboardState * capacity
        state_buffer = state_array_type()
        count = self._lib.ws_ascii_build_sequence(
            text_buffer,
            len(encoded),
            state_buffer,
            capacity,
        )
        self.assertEqual(
            count,
            capacity,
            msg="Each character should yield a press and release report",
        )
        return [state_buffer[i].modifiers for i in range(count)]

    def test_mixed_case_string_releases_modifiers_before_digits(self) -> None:
        text = "Hello from ESP32!"
        modifiers = self._build_sequence(text)

        uppercase_indices = [i for i, ch in enumerate(text) if ch.isalpha() and ch.isupper()]
        for index in uppercase_indices:
            press_index = index * 2
            self.assertEqual(
                modifiers[press_index],
                0x02,
                msg=f"Uppercase character {text[index]!r} should set the shift modifier",
            )
            self.assertEqual(
                modifiers[press_index + 1],
                0x00,
                msg=f"Uppercase character {text[index]!r} must release the modifier",
            )

        digit_indices = [i for i, ch in enumerate(text) if ch.isdigit()]
        for index in digit_indices:
            press_index = index * 2
            self.assertEqual(
                modifiers[press_index - 1],
                0x00,
                msg=f"Digit {text[index]!r} should be preceded by a modifier release",
            )
            self.assertEqual(
                modifiers[press_index],
                0x00,
                msg=f"Digit {text[index]!r} should not require modifiers",
            )
            self.assertEqual(
                modifiers[press_index + 1],
                0x00,
                msg=f"Digit {text[index]!r} must end with a zeroed report",
            )

        # Sanity check for punctuation that requires modifiers ("!").
        exclamation_index = text.index("!")
        press_index = exclamation_index * 2
        self.assertEqual(modifiers[press_index], 0x02)
        self.assertEqual(modifiers[press_index + 1], 0x00)


if __name__ == "__main__":
    unittest.main()
