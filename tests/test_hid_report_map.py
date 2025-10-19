import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BLE_HID_C = ROOT / "main" / "ble_hid.c"
REFERENCE_PY = ROOT / "tmp_composite_example.py"


def _extract_hex_values(text: str, pattern: str) -> list[int]:
    match = re.search(pattern, text, re.DOTALL)
    if not match:
        raise AssertionError("Pattern not found")
    return [int(token, 16) for token in re.findall(r"0x[0-9A-Fa-f]+", match.group(1))]


def _load_c_descriptor() -> list[int]:
    text = BLE_HID_C.read_text(encoding="utf-8")
    return _extract_hex_values(text, r"hid_report_map\[]\s*=\s*\{([^}]*)\}")


def _load_reference(name: str) -> list[int]:
    text = REFERENCE_PY.read_text(encoding="utf-8")
    return _extract_hex_values(text, rf"self.{name}\s*=\s*\[(.*?)\]")


class HidReportMapTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.c_descriptor = _load_c_descriptor()
        cls.mouse_reference = _load_reference("mouse_report")
        cls.keyboard_reference = _load_reference("keyboard_report")
        cls.consumer_reference = [
            0x05,
            0x0C,
            0x09,
            0x01,
            0xA1,
            0x01,
            0x85,
            0x03,
            0x15,
            0x00,
            0x26,
            0xFF,
            0x03,
            0x19,
            0x00,
            0x2A,
            0xFF,
            0x03,
            0x75,
            0x10,
            0x95,
            0x01,
            0x81,
            0x00,
            0xC0,
        ]

    def test_mouse_descriptor_matches_reference(self) -> None:
        mouse_slice = self.c_descriptor[: len(self.mouse_reference)]
        self.assertListEqual(mouse_slice, self.mouse_reference)

    def test_keyboard_descriptor_matches_reference(self) -> None:
        start = len(self.mouse_reference)
        end = start + len(self.keyboard_reference)
        self.assertListEqual(self.c_descriptor[start:end], self.keyboard_reference)

    def test_consumer_descriptor_has_expected_structure(self) -> None:
        start = len(self.mouse_reference) + len(self.keyboard_reference)
        self.assertListEqual(self.c_descriptor[start:], self.consumer_reference)

    def test_report_ids_are_contiguous(self) -> None:
        ids = [
            self.c_descriptor[index + 1]
            for index, value in enumerate(self.c_descriptor)
            if value == 0x85
        ]
        self.assertListEqual(ids, [1, 2, 3])


if __name__ == "__main__":
    unittest.main()
