#!/usr/bin/env python3
import importlib.util
import io
import json
import os
import socket
import sys
import subprocess
import tempfile
import threading
import unittest
from unittest import mock


ROOT = os.path.dirname(os.path.dirname(__file__))
SPEC = importlib.util.spec_from_file_location("esim_computer", os.path.join(ROOT, "tools", "esim-computer.py"))
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules["esim_computer"] = MODULE
SPEC.loader.exec_module(MODULE)


class EsimComputerTests(unittest.TestCase):
    def test_activation_code_is_parsed_without_retaining_secret(self):
        host, matching = MODULE.parse_activation("LPA:1$SMDP.EXAMPLE$MATCH-1")
        self.assertEqual((host, matching), ("smdp.example", "MATCH-1"))
        with self.assertRaises(MODULE.ComputerError):
            MODULE.parse_activation("LPA:1$smdp.example/path$bad")

    def test_authority_rejects_local_and_non_https(self):
        with self.assertRaises(MODULE.ComputerError):
            MODULE._authority("http://smdp.example")
        with self.assertRaises(MODULE.ComputerError):
            MODULE._authority("smdp.example/path")
        with self.assertRaises(MODULE.ComputerError):
            MODULE._public_addresses("127.0.0.1", 443)

    def test_connect_proxy_only_accepts_allowlist(self):
        target = socket.socket()
        target.bind(("127.0.0.1", 0))
        target.listen(1)
        port = target.getsockname()[1]
        received = []

        def serve():
            client, _ = target.accept()
            received.append(client.recv(4))
            client.sendall(b"pong")
            client.close()

        thread = threading.Thread(target=serve)
        thread.start()
        fake_addresses = [(socket.AF_INET, socket.SOCK_STREAM, 0, "allowed.example", ("127.0.0.1", port))]
        with mock.patch.object(MODULE, "_public_addresses", return_value=fake_addresses):
            proxy = MODULE.ConnectProxy([f"allowed.example:{port}"], timeout=10)
        try:
            client = socket.create_connection(("127.0.0.1", proxy.host_port), timeout=2)
            client.sendall(f"CONNECT allowed.example:{port} HTTP/1.1\r\n\r\n".encode())
            self.assertIn(b"200 Connection Established", client.recv(128))
            client.sendall(b"ping")
            self.assertEqual(client.recv(4), b"pong")
            client.close()
            denied = socket.create_connection(("127.0.0.1", proxy.host_port), timeout=2)
            denied.sendall(b"CONNECT other.example:443 HTTP/1.1\r\n\r\n")
            self.assertEqual(denied.recv(128), b"")
            denied.close()
        finally:
            proxy.close()
            target.close()
            thread.join(timeout=2)
        self.assertEqual(received, [b"ping"])

    def test_remote_run_keeps_secret_in_stdin(self):
        secret = "LPA:1$smdp.example$private"
        seen = {}

        def fake_run(argv, **kwargs):
            seen["argv"] = argv
            seen["input"] = kwargs.get("input")
            return subprocess.CompletedProcess(argv, 0, b'{"job":{"state":"done"}}\n', b"")

        with mock.patch.object(MODULE.subprocess, "run", side_effect=fake_run):
            result = MODULE.remote_run("USB123", {"action": "download", "activation_code": secret}, 5)
        self.assertEqual(result["job"]["state"], "done")
        self.assertNotIn(secret, " ".join(seen["argv"]))
        self.assertIn(secret.encode(), seen["input"])
        self.assertIn("flock -n 9", seen["argv"][-1])

    def test_reverse_removes_only_its_own_mapping(self):
        calls = []

        def fake_run(argv, **kwargs):
            calls.append(argv)
            return subprocess.CompletedProcess(argv, 0, b"", b"")

        reverse = MODULE.Reverse("USB123")
        with mock.patch.object(MODULE.subprocess, "run", side_effect=fake_run):
            reverse.open(23456)
            reverse.close()
        self.assertIn("--no-rebind", calls[0])
        self.assertIn("tcp:23456", calls[0])
        self.assertEqual(calls[1][-2:], ["--remove", f"tcp:{reverse.device_port}"])
        self.assertNotIn("--remove-all", calls[1])

    def test_timeout_is_unknown_and_is_not_retried(self):
        calls = []

        def timeout(*args, **kwargs):
            calls.append(1)
            raise subprocess.TimeoutExpired(kwargs.get("args", args[0]), 1)

        with mock.patch.object(MODULE.subprocess, "run", side_effect=timeout):
            with self.assertRaises(MODULE.ComputerError) as context:
                MODULE.remote_run("USB123", {"action": "download"}, 1)
        self.assertIn("结果未知", str(context.exception))
        self.assertEqual(calls, [1])

    def test_stdin_request_requires_explicit_eid_and_yes(self):
        args = mock.Mock(command="download", request_stdin=True, yes=False)
        args.serial = "USB123"
        with mock.patch.object(MODULE, "choose_serial", return_value="USB123"), mock.patch.object(
            MODULE, "scan", return_value={"view": {"eid": "1" * 32}}
        ), mock.patch.object(MODULE, "_read_request", return_value={"activation_code": "LPA:1$smdp.example$x"}):
            with self.assertRaises(MODULE.ComputerError):
                MODULE.command_download(args)

    def test_status_masks_result_iccid_too(self):
        value = {"job": {"result": {"iccid": "8944476500001234567"}}, "view": {"eid": "1" * 32}}
        masked = MODULE.masked_status(value)
        self.assertNotEqual(masked["job"]["result"]["iccid"], value["job"]["result"]["iccid"])
        self.assertEqual(masked["view"]["eid"], "1111" + "*" * 24 + "1111")


if __name__ == "__main__":
    unittest.main()
