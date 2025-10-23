import ctypes
import subprocess
import tempfile
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
MAIN_DIR = PROJECT_ROOT / "main"
STUB_DIR = PROJECT_ROOT / "tests" / "stubs"

ESP_OK = 0
WIFI_MODE_STA = 1
WIFI_MODE_APSTA = 3
WIFI_EVENT_STA_START = 0
IP_EVENT_STA_GOT_IP = 0


class Ip4Addr(ctypes.Structure):
    _fields_ = [("addr", ctypes.c_uint32)]


class EspNetifIpInfo(ctypes.Structure):
    _fields_ = [
        ("ip", Ip4Addr),
        ("netmask", Ip4Addr),
        ("gw", Ip4Addr),
    ]


class IpEventGotIp(ctypes.Structure):
    _fields_ = [
        ("esp_netif", ctypes.c_void_p),
        ("ip_info", EspNetifIpInfo),
    ]


class WifiManagerScanTransitionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._lib = cls._build_library()
        cls._configure_signatures()

    @classmethod
    def _build_library(cls) -> ctypes.CDLL:
        with tempfile.TemporaryDirectory() as tmpdir:
            library_path = Path(tmpdir) / "libwifi_manager_test.so"
            compile_cmd = [
                "gcc",
                "-std=c11",
                "-DUNIT_TEST",
                "-shared",
                "-fPIC",
                "-I",
                str(STUB_DIR),
                "-I",
                str(STUB_DIR / "freertos"),
                "-I",
                str(STUB_DIR / "lwip"),
                "-I",
                str(MAIN_DIR),
                str(MAIN_DIR / "wifi_credentials.c"),
                str(MAIN_DIR / "wifi_manager.c"),
                str(STUB_DIR / "wifi_manager_test_stubs.c"),
                "-o",
                str(library_path),
            ]
            subprocess.check_call(compile_cmd, cwd=PROJECT_ROOT)
            return ctypes.CDLL(str(library_path))

    @classmethod
    def _configure_signatures(cls) -> None:
        lib = cls._lib
        lib.wifi_manager_start_sta.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        lib.wifi_manager_start_sta.restype = ctypes.c_int
        lib.wifi_manager_is_scanning.restype = ctypes.c_bool
        lib.wifi_manager_is_connected.restype = ctypes.c_bool
        lib.wifi_manager_is_connecting.restype = ctypes.c_bool
        lib.wifi_manager_get_mode.restype = ctypes.c_int
        lib.wifi_manager_test_reset_state.restype = None
        lib.wifi_manager_test_reset_stubs.restype = None
        lib.wifi_manager_test_set_state.argtypes = [ctypes.c_bool, ctypes.c_bool, ctypes.c_int]
        lib.wifi_manager_test_set_state.restype = None
        lib.wifi_manager_test_get_deferred_ap_restore.restype = ctypes.c_bool
        lib.wifi_manager_test_invoke_event.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p]
        lib.wifi_manager_test_invoke_event.restype = None
        lib.wifi_manager_test_invoke_wifi_event.argtypes = [ctypes.c_int, ctypes.c_void_p]
        lib.wifi_manager_test_invoke_wifi_event.restype = None
        lib.wifi_manager_test_invoke_ip_event.argtypes = [ctypes.c_int, ctypes.c_void_p]
        lib.wifi_manager_test_invoke_ip_event.restype = None
        lib.wifi_manager_test_get_scan_stop_calls.restype = ctypes.c_int
        lib.wifi_manager_test_get_connect_calls.restype = ctypes.c_int
        lib.wifi_manager_test_get_wifi_mode.restype = ctypes.c_int

    def setUp(self) -> None:
        self._lib.wifi_manager_test_reset_stubs()
        self._lib.wifi_manager_test_reset_state()

    @property
    def _lib(self) -> ctypes.CDLL:
        return self.__class__._lib

    def test_scan_abort_allows_sta_connection(self) -> None:
        self._lib.wifi_manager_test_set_state(True, True, WIFI_MODE_APSTA)

        result = self._lib.wifi_manager_start_sta(b"TestSSID", b"password123")
        self.assertEqual(result, ESP_OK)

        self.assertEqual(self._lib.wifi_manager_test_get_scan_stop_calls(), 1)
        self.assertFalse(self._lib.wifi_manager_is_scanning())
        self.assertTrue(self._lib.wifi_manager_test_get_deferred_ap_restore())
        self.assertEqual(self._lib.wifi_manager_get_mode(), WIFI_MODE_STA)

        self._lib.wifi_manager_test_invoke_wifi_event(WIFI_EVENT_STA_START, None)
        self.assertEqual(self._lib.wifi_manager_test_get_connect_calls(), 1)

        got_ip = IpEventGotIp()
        got_ip.ip_info.ip.addr = 0x01020304
        self._lib.wifi_manager_test_invoke_ip_event(IP_EVENT_STA_GOT_IP, ctypes.byref(got_ip))

        self.assertTrue(self._lib.wifi_manager_is_connected())
        self.assertFalse(self._lib.wifi_manager_is_connecting())
        self.assertFalse(self._lib.wifi_manager_test_get_deferred_ap_restore())
        self.assertEqual(self._lib.wifi_manager_test_get_wifi_mode(), WIFI_MODE_STA)


if __name__ == "__main__":
    unittest.main()
