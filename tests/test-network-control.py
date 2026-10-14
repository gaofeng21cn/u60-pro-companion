#!/usr/bin/env python3
import json
import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "panel" / "network-control.sh"


class NetworkControlTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.env = dict(os.environ,
                        NETWORK_CONTROL_ROOT=str(self.root / "network-control"),
                        PROFILE_ROOT=str(self.root / "panel"),
                        DHCP_LEASES=str(self.root / "leases"),
                        IPTABLES="/bin/false", IP6TABLES="/bin/false",
                        IP="/bin/false")
        (self.root / "panel").mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def execute(self, action, data=None):
        return subprocess.run(["sh", str(SCRIPT), action], input=data,
                              text=True, capture_output=True, env=self.env)

    def test_shell_syntax_and_status_are_read_only(self):
        syntax = subprocess.run(["sh", "-n", str(SCRIPT)], capture_output=True, text=True)
        self.assertEqual(syntax.returncode, 0, syntax.stderr)
        result = self.execute("status")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / "network-control").exists())
        body = json.loads(result.stdout)
        self.assertEqual(body["profile"], "direct")
        self.assertFalse(body["udp_tproxy_hook"])

    def test_clients_exposes_lease_and_never_invents_policy(self):
        (self.root / "leases").write_text("1 AA:BB:CC:DD:EE:FF 192.168.1.20 phone *\n")
        body = json.loads(self.execute("clients").stdout)
        self.assertEqual(body["clients"][0]["mac"], "aa:bb:cc:dd:ee:ff")
        self.assertEqual(body["clients"][0]["policy"], "unmanaged")

    def test_off_without_optional_dependencies_cleans_owned_state(self):
        state = self.root / "network-control"
        state.mkdir()
        (state / "manifest").write_text("udp=1 guard6=1\naa:bb:cc:dd:ee:ff 17920\n")
        (state / "resources-owned").write_text("owned\n")
        result = self.execute("off")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertFalse((state / "manifest").exists())
        self.assertFalse((state / "resources-owned").exists())

if __name__ == "__main__":
    unittest.main()
