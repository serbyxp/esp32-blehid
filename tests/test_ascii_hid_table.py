import unittest

from ascii_hid_table import HID_ASCII_TABLE, HID_KEYMAP_SHIFT


class AsciiHidTableTest(unittest.TestCase):
    def test_table_has_expected_length(self) -> None:
        self.assertEqual(len(HID_ASCII_TABLE), 128)

    def test_printable_ascii_characters_have_mappings(self) -> None:
        for ascii_code in range(32, 127):  # Printable ASCII range
            entry = HID_ASCII_TABLE[ascii_code]
            with self.subTest(ascii_code=ascii_code, char=chr(ascii_code)):
                self.assertNotEqual(
                    entry,
                    0,
                    msg=f"Printable character {chr(ascii_code)!r} must have a mapping",
                )
                keycode = entry & 0x7F
                self.assertNotEqual(
                    keycode,
                    0,
                    msg=f"Printable character {chr(ascii_code)!r} must map to a valid keycode",
                )

    def test_shift_bit_only_sets_modifier_flag(self) -> None:
        for ascii_code, entry in enumerate(HID_ASCII_TABLE):
            if entry == 0:
                continue
            keycode = entry & 0x7F
            self.assertGreaterEqual(keycode, 0)
            self.assertLess(keycode, 0x80)
            self.assertIn(entry - keycode, (0, HID_KEYMAP_SHIFT))


if __name__ == "__main__":
    unittest.main()
