import ctypes
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
MAIN_DIR = PROJECT_ROOT / "main"
STUB_DIR = PROJECT_ROOT / "tests" / "stubs"

class WifiManagerWaitForConnectionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._lib = cls._build_library()
        cls._configure_signatures()

    @classmethod
    def _build_library(cls) -> ctypes.CDLL:
        with tempfile.TemporaryDirectory() as tmpdir:
            library_path = Path(tmpdir) / "libwifi_manager_wait_test.so"
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
        lib.wifi_manager_test_reset_state.restype = None
        lib.wifi_manager_test_reset_stubs.restype = None
        lib.wifi_manager_test_set_connection_flags.argtypes = [ctypes.c_bool, ctypes.c_bool, ctypes.c_int]
        lib.wifi_manager_test_set_connection_flags.restype = None
        lib.wifi_manager_test_wait_for_sta_connection.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_bool)]
        lib.wifi_manager_test_wait_for_sta_connection.restype = ctypes.c_bool
        lib.wifi_manager_test_get_tick_count.restype = ctypes.c_uint32
        lib.wifi_manager_test_set_delay_hook.argtypes = [ctypes.c_void_p]
        lib.wifi_manager_test_set_delay_hook.restype = None

    def setUp(self) -> None:
        self._lib.wifi_manager_test_reset_stubs()
        self._lib.wifi_manager_test_reset_state()

    @property
    def _lib(self) -> ctypes.CDLL:
        return self.__class__._lib

    def _set_connection_state(self, connected: bool, connecting: bool, retry_count: int) -> None:
        self._lib.wifi_manager_test_set_connection_flags(connected, connecting, retry_count)

    def test_retry_activity_extends_timeout(self) -> None:
        # Configure initial state: attempting to connect with no retries yet recorded.
        self._set_connection_state(False, True, 0)

        # Schedule connection state updates tied to the fake FreeRTOS tick counter.
        events = [
            (100, (False, True, 1)),
            (200, (False, True, 2)),
            (300, (False, False, 0)),
        ]

        callback_type = ctypes.CFUNCTYPE(None, ctypes.c_uint32)

        def make_hook():
            remaining = events.copy()

            @callback_type
            def _hook(current_tick: int) -> None:
                while remaining and current_tick >= remaining[0][0]:
                    _, state = remaining.pop(0)
                    self._set_connection_state(*state)

                # Once all scripted events have fired, remove the hook to avoid
                # redundant calls on later delays.
                if not remaining:
                    self._lib.wifi_manager_test_set_delay_hook(None)

            _hook.remaining = remaining  # type: ignore[attr-defined]
            return _hook

        hook = make_hook()
        self._lib.wifi_manager_test_set_delay_hook(hook)

        timed_out = ctypes.c_bool(False)
        result_holder: dict[str, bool] = {}

        def wait_for_connection() -> None:
            res = self._lib.wifi_manager_test_wait_for_sta_connection(100, ctypes.byref(timed_out))
            result_holder["result"] = res

        thread = threading.Thread(target=wait_for_connection)
        thread.start()
        thread.join(timeout=1.5)

        self.assertFalse(thread.is_alive(), "wait_for_sta_connection did not return")
        self.assertIn("result", result_holder)
        self.assertFalse(result_holder["result"])
        self.assertFalse(timed_out.value)

        # The fake tick counter should have advanced beyond the initial timeout,
        # demonstrating that activity resets the watchdog window.
        self.assertGreaterEqual(self._lib.wifi_manager_test_get_tick_count(), 300)


if __name__ == "__main__":
    unittest.main()
