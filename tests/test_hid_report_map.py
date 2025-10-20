import ctypes
import re
import subprocess
import tempfile
import textwrap
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
            0x05,
            0x0C,
            0x15,
            0x00,
            0x25,
            0x01,
            0x75,
            0x01,
            0x95,
            0x10,
            0x09,
            0xB5,
            0x09,
            0xB6,
            0x09,
            0xB7,
            0x09,
            0xCD,
            0x09,
            0xE2,
            0x09,
            0xE9,
            0x09,
            0xEA,
            0x0A,
            0x23,
            0x02,
            0x0A,
            0x94,
            0x01,
            0x0A,
            0x92,
            0x01,
            0x0A,
            0x2A,
            0x02,
            0x0A,
            0x21,
            0x02,
            0x0A,
            0x26,
            0x02,
            0x0A,
            0x24,
            0x02,
            0x0A,
            0x83,
            0x01,
            0x0A,
            0x8A,
            0x01,
            0x81,
            0x02,
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


class ConsumerUsageMaskTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source_text = BLE_HID_C.read_text(encoding="utf-8")
        cls.consumer_usages = _extract_hex_values(
            cls.source_text, r"s_consumer_usages\[]\s*=\s*\{([^}]*)\}"
        )

        array_src = re.search(
            r"static const uint16_t s_consumer_usages\[] = \{[^}]*\};",
            cls.source_text,
            re.DOTALL,
        )
        if array_src is None:
            raise AssertionError("Failed to locate consumer usage table")

        func_src = re.search(
            r"uint16_t\s+ble_hid_consumer_usage_to_mask\(uint16_t usage\)\s*\{.*?\n\}",
            cls.source_text,
            re.DOTALL,
        )
        if func_src is None:
            raise AssertionError("Failed to locate consumer usage helper")

        helper_code = textwrap.dedent(
            """
            #include <stdint.h>
            #include <stddef.h>
            {array}
            {function}
            """
        ).format(array=array_src.group(0), function=func_src.group(0))

        cls._tmpdir = tempfile.TemporaryDirectory()
        c_path = Path(cls._tmpdir.name) / "consumer_mask.c"
        so_path = Path(cls._tmpdir.name) / "consumer_mask.so"
        c_path.write_text(helper_code, encoding="utf-8")

        subprocess.run(
            ["gcc", "-shared", "-fPIC", str(c_path), "-o", str(so_path)],
            check=True,
            capture_output=True,
        )

        cls.lib = ctypes.CDLL(str(so_path))
        cls.lib.ble_hid_consumer_usage_to_mask.argtypes = [ctypes.c_uint16]
        cls.lib.ble_hid_consumer_usage_to_mask.restype = ctypes.c_uint16

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmpdir.cleanup()

    def _assert_usage_maps_to_expected_bit(self, usage: int) -> None:
        try:
            index = self.consumer_usages.index(usage)
        except ValueError as exc:  # pragma: no cover - defensive
            raise AssertionError(f"Usage 0x{usage:04X} not present in descriptor") from exc

        mask = self.lib.ble_hid_consumer_usage_to_mask(usage)
        expected_mask = 1 << index
        self.assertEqual(mask, expected_mask)

        low_byte = mask & 0xFF
        high_byte = (mask >> 8) & 0xFF

        if index < 8:
            self.assertNotEqual(low_byte, 0)
            self.assertEqual(high_byte, 0)
        else:
            self.assertEqual(low_byte, 0)
            self.assertNotEqual(high_byte, 0)

    def test_volume_up_maps_to_low_byte_bit(self) -> None:
        self._assert_usage_maps_to_expected_bit(0x00E9)

    def test_play_pause_maps_to_low_byte_bit(self) -> None:
        self._assert_usage_maps_to_expected_bit(0x00CD)

    def test_mail_maps_to_high_byte_bit(self) -> None:
        self._assert_usage_maps_to_expected_bit(0x018A)

if __name__ == "__main__":
    unittest.main()
