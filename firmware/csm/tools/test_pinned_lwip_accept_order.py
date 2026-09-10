import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build/vendor-src/mbed-os-17dc3dc2/connectivity/lwipstack/source/LWIPStack.cpp"

class PinnedLwipAcceptOrderTest(unittest.TestCase):
    def test_no_pending_accept_does_not_allocate_arena_slot(self):
        text = SOURCE.read_text(encoding="utf-8")
        begin = text.index("nsapi_error_t LWIP::socket_accept(")
        end = text.index("nsapi_size_or_error_t LWIP::socket_send(", begin)
        body = text[begin:end]
        self.assertLess(body.index("netconn_accept"), body.index("arena_alloc"))
        self.assertIn("return err_remap(err);", body)

    def test_full_arena_releases_accepted_netconn_and_preserves_listener(self):
        text = SOURCE.read_text(encoding="utf-8")
        begin = text.index("nsapi_error_t LWIP::socket_accept(")
        end = text.index("nsapi_size_or_error_t LWIP::socket_send(", begin)
        body = text[begin:end]
        self.assertIn("netconn_delete(accepted)", body)
        self.assertIn("return NSAPI_ERROR_NO_SOCKET", body)
        self.assertLess(body.index("netconn_accept"), body.index("arena_alloc"))
        self.assertLess(body.index("arena_alloc"), body.index("netconn_set_nonblocking"))

if __name__ == "__main__":
    unittest.main()
