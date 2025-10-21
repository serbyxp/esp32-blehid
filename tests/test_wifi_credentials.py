import ctypes
import subprocess
import tempfile
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
MAIN_DIR = PROJECT_ROOT / "main"


class WifiCredentialsValidationTest(unittest.TestCase):
    WIFI_CREDENTIALS_OK = 0
    WIFI_CREDENTIALS_ERR_SSID_TOO_LONG = 1
    WIFI_CREDENTIALS_ERR_PSK_TOO_SHORT = 2
    WIFI_CREDENTIALS_ERR_PSK_TOO_LONG = 3

    @classmethod
    def setUpClass(cls) -> None:
        cls._lib = cls._build_test_library()
        cls._lib.wifi_credentials_validate.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        cls._lib.wifi_credentials_validate.restype = ctypes.c_int
        cls._lib.wifi_credentials_error_to_string.argtypes = [ctypes.c_int]
        cls._lib.wifi_credentials_error_to_string.restype = ctypes.c_char_p

    @staticmethod
    def _build_test_library() -> ctypes.CDLL:
        with tempfile.TemporaryDirectory() as tmpdir:
            library_path = Path(tmpdir) / "libwifi_credentials.so"
            compile_cmd = [
                "gcc",
                "-std=c11",
                "-shared",
                "-fPIC",
                "-I",
                str(PROJECT_ROOT / "tests" / "stubs"),
                "-I",
                str(MAIN_DIR),
                str(MAIN_DIR / "wifi_credentials.c"),
                "-o",
                str(library_path),
            ]
            subprocess.check_call(compile_cmd, cwd=PROJECT_ROOT)
            return ctypes.CDLL(str(library_path))

    def _validate(self, ssid: bytes, psk: bytes) -> int:
        return self._lib.wifi_credentials_validate(ctypes.c_char_p(ssid), ctypes.c_char_p(psk))

    def _error_label(self, error_code: int) -> str:
        return self._lib.wifi_credentials_error_to_string(error_code).decode()

    def test_accepts_valid_psk_lower_bound(self) -> None:
        psk = b"a" * 8
        result = self._validate(b"ssid", psk)
        self.assertEqual(result, self.WIFI_CREDENTIALS_OK)

    def test_accepts_valid_psk_upper_bound(self) -> None:
        psk = b"b" * 63
        result = self._validate(b"ssid", psk)
        self.assertEqual(result, self.WIFI_CREDENTIALS_OK)

    def test_rejects_too_short_psk(self) -> None:
        result = self._validate(b"ssid", b"short")
        self.assertEqual(result, self.WIFI_CREDENTIALS_ERR_PSK_TOO_SHORT)
        self.assertEqual(self._error_label(result), "psk_too_short")

    def test_rejects_too_long_psk(self) -> None:
        result = self._validate(b"ssid", b"c" * 64)
        self.assertEqual(result, self.WIFI_CREDENTIALS_ERR_PSK_TOO_LONG)
        self.assertEqual(self._error_label(result), "psk_too_long")


if __name__ == "__main__":
    unittest.main()
