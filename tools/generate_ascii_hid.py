#!/usr/bin/env python3
"""Generate shared ASCII to HID lookup tables for firmware and tooling."""
from __future__ import annotations

from pathlib import Path
from textwrap import indent

# Canonical ASCII -> HID table. Values use the HID_KEYMAP_SHIFT bit (0x80)
# to indicate that the key requires the left shift modifier.
ASCII_HID_TABLE = [
    0, 0, 0, 0, 0, 0, 0, 0, 42, 43, 40, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    44, 158, 180, 160, 161, 162, 164, 52, 166, 167, 165, 174, 54, 45, 55, 56,
    39, 30, 31, 32, 33, 34, 35, 36, 37, 38, 179, 51, 182, 46, 183, 184,
    159, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146,
    147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 47, 49, 48, 163, 173,
    53, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 175, 177, 176, 181, 0,
]

HEADER_ARRAY_NAME = "hid_ascii_table"
HEADER_SIZE_NAME = "HID_ASCII_TABLE_SIZE"

ROOT = Path(__file__).resolve().parents[1]
HEADER_PATH = ROOT / "main" / "hid_ascii_table.h"
PY_MODULE_PATH = ROOT / "ascii_hid_table.py"
ASCII_ARRAY_PATH = ROOT / "ascii_array.txt"


def _format_table(entries: list[int], values_per_line: int = 16) -> str:
    lines = []
    for i in range(0, len(entries), values_per_line):
        chunk = ", ".join(str(v) for v in entries[i : i + values_per_line])
        lines.append(f"    {chunk},")
    if lines:
        lines[-1] = lines[-1].rstrip(',')
    return "\n".join(lines)


def write_header(path: Path) -> None:
    formatted = _format_table(ASCII_HID_TABLE)
    content = (
        "#pragma once\n"
        "#include <stdint.h>\n\n"
        f"#define {HEADER_SIZE_NAME} {len(ASCII_HID_TABLE)}\n"
        f"static const uint8_t {HEADER_ARRAY_NAME}[{HEADER_SIZE_NAME}] = {{\n"
        f"{formatted}\n"
        "};\n"
    )
    path.write_text(content + "\n", encoding="utf-8")


def write_python_module(path: Path) -> None:
    formatted = _format_table(ASCII_HID_TABLE)
    content = (
        '"""Generated ASCII to HID lookup table shared with firmware."""\n'
        "HID_KEYMAP_SHIFT = 0x80\n"
        "HID_KEYMAP_LEFT_SHIFT = 0x02\n"
        "HID_ASCII_TABLE = [\n"
        f"{formatted}\n"
        "]\n"
    )
    path.write_text(content + "\n", encoding="utf-8")


def write_ascii_text(path: Path) -> None:
    formatted = _format_table(ASCII_HID_TABLE)
    content = "[\n" + indent(formatted, "    ") + "\n]"
    path.write_text(content + "\n", encoding="utf-8")


def main() -> None:
    if len(ASCII_HID_TABLE) != 128:
        raise SystemExit("ASCII_HID_TABLE must contain exactly 128 entries")

    write_header(HEADER_PATH)
    write_python_module(PY_MODULE_PATH)
    write_ascii_text(ASCII_ARRAY_PATH)


if __name__ == "__main__":
    main()
