import pathlib
import shutil
import subprocess
import tempfile
import unittest


class DriverPacketTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "g++ is required for native packet checks")
    def test_packet_protocol(self):
        root = pathlib.Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(prefix="spincoater-packet-test-") as temporary:
            binary = pathlib.Path(temporary) / "driver_packet_test"
            subprocess.run([
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-I", str(root),
                str(root / "driver_packet.cpp"),
                str(root / "tools/tests/driver_packet_test.cpp"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
