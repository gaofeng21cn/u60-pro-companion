#!/usr/bin/env python3
"""Use the computer's network for a removable eUICC in a U60 Pro.

The modem, lpac and TLS verification remain on the device.  This program only
provides a short-lived, loopback CONNECT tunnel carried by ``adb reverse`` and
invokes the existing one-shot worker while holding its job lock.
"""

from __future__ import annotations

import argparse
import getpass
import ipaddress
import json
import random
import re
import selectors
import socket
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from typing import Iterable
from urllib.parse import urlsplit


MAX_CONNECT_HEADER = 16 * 1024
MAX_TUNNEL_BYTES = 64 * 1024 * 1024
DEFAULT_DOWNLOAD_TIMEOUT = 700
HOST_RE = re.compile(r"^[A-Za-z0-9.-]+$")


class ComputerError(Exception):
    pass


def parse_activation(code: str) -> tuple[str, str]:
    value = code.strip()
    if not value[:4].lower() == "lpa:" or value[4:6] != "1$":
        raise ComputerError("激活码格式无效")
    parts = value[6:].split("$")
    if len(parts) > 3 or not parts[0] or len(parts[0]) > 255:
        raise ComputerError("激活码格式无效")
    smdp = parts[0].lower()
    matching = parts[1] if len(parts) > 1 else ""
    if not re.fullmatch(r"[A-Za-z0-9.-]+(?::[0-9]{1,5})?", smdp):
        raise ComputerError("激活码格式无效")
    if matching and not re.fullmatch(r"[A-Za-z0-9-]+", matching):
        raise ComputerError("激活码格式无效")
    return smdp, matching


def _authority(value: str) -> tuple[str, int]:
    value = value.strip()
    if not value:
        raise ComputerError("服务器地址为空")
    if "://" not in value:
        value = "https://" + value
    parsed = urlsplit(value)
    if (
        parsed.scheme.lower() != "https"
        or parsed.path not in ("", "/")
        or parsed.query
        or parsed.fragment
        or parsed.username is not None
        or parsed.password is not None
    ):
        raise ComputerError("服务器地址必须是 HTTPS authority")
    host = parsed.hostname
    if not host or not HOST_RE.fullmatch(host) or host.startswith(".") or host.endswith("."):
        raise ComputerError("服务器地址无效")
    try:
        port = parsed.port or 443
    except ValueError as exc:
        raise ComputerError("服务器端口无效") from exc
    if not 1 <= port <= 65535:
        raise ComputerError("服务器端口无效")
    return host.lower(), port


def _public_addresses(host: str, port: int) -> list[tuple[int, int, int, str, tuple]]:
    try:
        infos = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
    except OSError as exc:
        raise ComputerError("无法解析 eSIM 服务器") from exc
    result: list[tuple[int, int, int, str, tuple]] = []
    seen: set[tuple] = set()
    for family, socktype, proto, _, sockaddr in infos:
        address = ipaddress.ip_address(sockaddr[0])
        if (
            address.is_private
            or address.is_loopback
            or address.is_link_local
            or address.is_reserved
            or address.is_unspecified
            or address.is_multicast
            or address.version == 6 and (address.is_site_local or address.ipv4_mapped is not None)
        ):
            continue
        key = (family, socktype, proto, sockaddr)
        if key not in seen:
            result.append((family, socktype, proto, host, sockaddr))
            seen.add(key)
    if not result:
        raise ComputerError("eSIM 服务器地址解析到受限制网络")
    return result


@dataclass
class Target:
    host: str
    port: int
    addresses: list[tuple[int, int, int, str, tuple]]

class ConnectProxy:
    """A single-job CONNECT proxy bound to host loopback."""

    def __init__(self, authorities: Iterable[str], timeout: int = DEFAULT_DOWNLOAD_TIMEOUT):
        self._targets: dict[str, Target] = {}
        self._lock = threading.Lock()
        for item in authorities:
            self.allow(item)
        if not self._targets:
            raise ComputerError("没有可用的 eSIM 服务器")
        self._server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server.bind(("127.0.0.1", 0))
        self._server.listen(8)
        self._server.settimeout(0.5)
        self.host_port = self._server.getsockname()[1]
        self._deadline = time.monotonic() + timeout
        self._stop = threading.Event()
        self._threads: list[threading.Thread] = []
        self._acceptor = threading.Thread(target=self._accept, name="u60-esim-proxy", daemon=True)
        self._acceptor.start()

    def allow(self, value: str) -> None:
        host, port = _authority(value)
        key = f"{host}:{port}"
        with self._lock:
            if key not in self._targets:
                self._targets[key] = Target(host, port, _public_addresses(host, port))

    def _accept(self) -> None:
        while not self._stop.is_set() and time.monotonic() < self._deadline:
            try:
                client, _ = self._server.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            thread = threading.Thread(target=self._connection, args=(client,), daemon=True)
            self._threads.append(thread)
            thread.start()

    def _connection(self, client: socket.socket) -> None:
        client.settimeout(10)
        try:
            request = b""
            while b"\r\n\r\n" not in request and len(request) < MAX_CONNECT_HEADER:
                chunk = client.recv(4096)
                if not chunk:
                    return
                request += chunk
            if b"\r\n\r\n" not in request:
                return
            first = request.split(b"\r\n", 1)[0].decode("ascii", "strict")
            fields = first.split(" ")
            if len(fields) != 3 or fields[0] != "CONNECT" or fields[2] != "HTTP/1.1":
                return
            host, port = _authority(fields[1])
            key = f"{host}:{port}"
            with self._lock:
                target = self._targets.get(key)
            if target is None:
                return
            upstream = self._connect(target)
            if upstream is None:
                return
            client.sendall(b"HTTP/1.1 200 Connection Established\r\n\r\n")
            self._relay(client, upstream)
        except (OSError, UnicodeError, ValueError, ComputerError):
            return
        finally:
            try:
                client.close()
            except OSError:
                pass

    @staticmethod
    def _connect(target: Target) -> socket.socket | None:
        for family, socktype, proto, _, sockaddr in target.addresses:
            try:
                upstream = socket.socket(family, socktype, proto)
                upstream.settimeout(15)
                upstream.connect(sockaddr)
                return upstream
            except OSError:
                try:
                    upstream.close()
                except UnboundLocalError:
                    pass
        return None

    def _relay(self, left: socket.socket, right: socket.socket) -> None:
        left.settimeout(5)
        right.settimeout(5)
        selector = selectors.DefaultSelector()
        selector.register(left, selectors.EVENT_READ, right)
        selector.register(right, selectors.EVENT_READ, left)
        count = 0
        try:
            while not self._stop.is_set() and time.monotonic() < self._deadline:
                events = selector.select(timeout=1)
                if not events:
                    continue
                for key, _ in events:
                    data = key.fileobj.recv(64 * 1024)
                    if not data:
                        return
                    count += len(data)
                    if count > MAX_TUNNEL_BYTES:
                        return
                    key.data.sendall(data)
        except OSError:
            return
        finally:
            selector.close()
            try:
                right.close()
            except OSError:
                pass

    def close(self) -> None:
        self._stop.set()
        try:
            self._server.close()
        except OSError:
            pass
        self._acceptor.join(timeout=1)


def _adb_base(serial: str | None) -> list[str]:
    return ["adb", *(["-s", serial] if serial else [])]


def choose_serial(requested: str | None) -> str:
    result = subprocess.run(["adb", "devices"], capture_output=True, text=True, timeout=15)
    if result.returncode:
        raise ComputerError("无法连接 ADB")
    devices = []
    for line in result.stdout.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device" and not parts[0].startswith("emulator-"):
            devices.append(parts[0])
    if requested:
        if requested not in devices:
            raise ComputerError("指定的 USB 设备不可用")
        return requested
    if len(devices) != 1:
        raise ComputerError("请用 --serial 指定唯一的 USB 设备")
    return devices[0]


class Reverse:
    def __init__(self, serial: str):
        self.serial = serial
        self.device_port = random.randrange(20000, 50000)
        self.host_port: int | None = None
        self.created = False

    def open(self, host_port: int) -> None:
        self.host_port = host_port
        command = _adb_base(self.serial) + [
            "reverse", "--no-rebind", f"tcp:{self.device_port}", f"tcp:{host_port}"
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        if result.returncode:
            raise ComputerError("无法建立临时 ADB 网络入口")
        self.created = True

    def close(self) -> None:
        if not self.created:
            return
        subprocess.run(
            _adb_base(self.serial) + ["reverse", "--remove", f"tcp:{self.device_port}"],
            capture_output=True,
            text=True,
            timeout=15,
        )
        self.created = False


def remote_run(serial: str, request: dict, timeout: int) -> dict:
    payload = json.dumps(request, separators=(",", ":"), ensure_ascii=True).encode()
    command = (
        "exec 9>/tmp/u60-esim/job.lock; "
        "flock -n 9 || exit 75; "
        "exec /data/u60-panel/u60-esim run"
    )
    try:
        result = subprocess.run(
            _adb_base(serial) + ["shell", "-T", command],
            input=payload,
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        raise ComputerError("设备操作超时，写卡结果未知；请先重新读取卡片") from exc
    if result.returncode == 75:
        raise ComputerError("设备已有 eSIM 操作正在执行")
    if not result.stdout:
        raise ComputerError("设备连接中断，写卡结果未知；请先重新读取卡片")
    try:
        value = json.loads(result.stdout.decode().strip().splitlines()[-1])
    except (ValueError, IndexError, UnicodeDecodeError) as exc:
        raise ComputerError("设备返回无效结果，写卡结果未知；请先重新读取卡片") from exc
    if not isinstance(value, dict):
        raise ComputerError("设备返回无效结果，写卡结果未知；请先重新读取卡片")
    return value


def mask(value: str, head: int = 4, tail: int = 4) -> str:
    value = str(value)
    if len(value) <= head + tail:
        return "*" * len(value)
    return value[:head] + "*" * (len(value) - head - tail) + value[-tail:]


def masked_status(value: dict) -> dict:
    result = json.loads(json.dumps(value))
    job = result.get("job")
    if isinstance(job, dict) and isinstance(job.get("result"), dict):
        if isinstance(job["result"].get("iccid"), str):
            job["result"]["iccid"] = mask(job["result"]["iccid"])
    view = result.get("view")
    if isinstance(view, dict):
        if isinstance(view.get("eid"), str):
            view["eid"] = mask(view["eid"])
        for profile in view.get("profiles", []):
            if isinstance(profile, dict) and isinstance(profile.get("iccid"), str):
                profile["iccid"] = mask(profile["iccid"])
        for note in view.get("notifications", []):
            if isinstance(note, dict) and isinstance(note.get("iccid"), str):
                note["iccid"] = mask(note["iccid"])
    return result


def scan(serial: str) -> dict:
    return remote_run(serial, {"action": "scan"}, 120)


def _view_eid(value: dict) -> str:
    eid = value.get("view", {}).get("eid")
    if not isinstance(eid, str) or not re.fullmatch(r"[0-9]{32}", eid):
        raise ComputerError("无法读取有效的 eUICC EID")
    return eid


def _notification_targets(value: dict) -> list[str]:
    output = []
    for item in value.get("view", {}).get("notifications", []):
        address = item.get("address") if isinstance(item, dict) else None
        if isinstance(address, str) and address.strip():
            output.append(address)
    return output


def _fresh_notifications(before: set[int], value: dict) -> list[dict]:
    result = []
    for item in value.get("view", {}).get("notifications", []):
        if not isinstance(item, dict) or not isinstance(item.get("seq"), int):
            continue
        if item["seq"] not in before and item.get("operation") in ("install", "delete"):
            result.append(item)
    return result


def _allow_notifications(proxy: ConnectProxy, items: Iterable[dict]) -> list[dict]:
    """Add only valid, publicly resolvable notification authorities.

    A malformed or unreachable unrelated pending notification must stay on the
    card without preventing the requested SM-DP+ download.
    """
    allowed = []
    for item in items:
        address = item.get("address")
        if not isinstance(address, str) or not address.strip():
            continue
        try:
            proxy.allow(address)
        except ComputerError:
            continue
        allowed.append(item)
    return allowed


def _read_request(args: argparse.Namespace) -> dict:
    if args.request_stdin:
        try:
            value = json.load(sys.stdin)
        except (ValueError, OSError) as exc:
            raise ComputerError("stdin 不是有效 JSON 请求") from exc
        if not isinstance(value, dict):
            raise ComputerError("请求必须是 JSON 对象")
        return value
    if args.command != "download":
        raise ComputerError("自动化请求必须使用 --request-stdin")
    code = getpass.getpass("激活码（输入不回显）：")
    value = {"activation_code": code}
    if input("是否需要确认码？[y/N] ").strip().lower() == "y":
        value["confirmation_code"] = getpass.getpass("确认码（输入不回显）：")
    imei = input("可选 IMEI（直接回车跳过）：").strip()
    if imei:
        value["imei"] = imei
    return value


def command_scan(args: argparse.Namespace) -> int:
    serial = choose_serial(args.serial)
    print(json.dumps(masked_status(scan(serial)), ensure_ascii=False, indent=2))
    return 0


def command_download(args: argparse.Namespace) -> int:
    serial = choose_serial(args.serial)
    request = _read_request(args)
    initial = scan(serial)
    eid = _view_eid(initial)
    expected = request.get("expected_eid")
    if args.request_stdin and (args.yes is not True or not isinstance(expected, str)):
        raise ComputerError("自动化请求必须用 --yes 并明确提供 expected_eid")
    if expected is not None and expected != eid:
        raise ComputerError("请求中的 EID 与当前卡片不一致，未修改")
    request["expected_eid"] = eid
    if args.yes is not True and not args.request_stdin:
        answer = input(f"将向 EID 尾号 {eid[-4:]} 写入配置，继续？[y/N] ")
        if answer.strip().lower() != "y":
            print("已取消")
            return 0
    smdp = request.get("smdp")
    if not smdp and isinstance(request.get("activation_code"), str):
        smdp, _ = parse_activation(request["activation_code"])
    if not isinstance(smdp, str):
        raise ComputerError("请求缺少 activation_code 或 smdp")
    proxy = ConnectProxy([smdp])
    _allow_notifications(proxy, initial.get("view", {}).get("notifications", []))
    reverse = Reverse(serial)
    try:
        reverse.open(proxy.host_port)
        request.update({"route": "proxy", "proxy_url": f"http://127.0.0.1:{reverse.device_port}"})
        before = {n.get("seq") for n in initial.get("view", {}).get("notifications", []) if isinstance(n, dict)}
        result = remote_run(serial, {"action": "download", **request}, DEFAULT_DOWNLOAD_TIMEOUT)
        if result.get("job", {}).get("state") != "done":
            print(json.dumps(masked_status(result), ensure_ascii=False, indent=2))
            return 1
        after = result
        # A download can create a new notification address after the worker's
        # first attempt.  Add only those exact addresses, then process the
        # notification in a separate job; never retry the download itself.
        fresh = _fresh_notifications(before, after)
        if not fresh:
            reread = scan(serial)
            fresh = _fresh_notifications(before, reread)
            after = reread
        if fresh:
            fresh = _allow_notifications(proxy, fresh)
        if fresh:
            notes = {
                "action": "notifications",
                "operation": "process",
                "seq": [item["seq"] for item in fresh],
                "expected_eid": eid,
                "route": "proxy",
                "proxy_url": f"http://127.0.0.1:{reverse.device_port}",
            }
            note_result = remote_run(serial, notes, 240)
            after = note_result
        print(json.dumps(masked_status(after), ensure_ascii=False, indent=2))
        return 0 if after.get("job", {}).get("state") == "done" else 1
    finally:
        reverse.close()
        proxy.close()


def command_notify(args: argparse.Namespace) -> int:
    serial = choose_serial(args.serial)
    request = _read_request(args)
    initial = scan(serial)
    eid = _view_eid(initial)
    if request.get("expected_eid", eid) != eid:
        raise ComputerError("请求中的 EID 与当前卡片不一致，未修改")
    seq = request.get("seq")
    if not isinstance(seq, list) or not seq or not all(isinstance(x, int) for x in seq):
        raise ComputerError("请求缺少 seq")
    authorities = _notification_targets(initial)
    if not authorities:
        raise ComputerError("卡片没有可用的通知地址")
    proxy = None
    for authority in authorities:
        try:
            proxy = ConnectProxy([authority])
            break
        except ComputerError:
            continue
    if proxy is None:
        raise ComputerError("卡片通知地址无法连接")
    _allow_notifications(proxy, initial.get("view", {}).get("notifications", []))
    reverse = Reverse(serial)
    try:
        reverse.open(proxy.host_port)
        result = remote_run(
            serial,
            {"action": "notifications", "operation": "process", "seq": seq,
             "expected_eid": eid, "route": "proxy",
             "proxy_url": f"http://127.0.0.1:{reverse.device_port}"},
            240,
        )
        print(json.dumps(masked_status(result), ensure_ascii=False, indent=2))
        return 0 if result.get("job", {}).get("state") == "done" else 1
    finally:
        reverse.close()
        proxy.close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="用电脑网络为 U60 Pro 可插拔 eUICC 执行安全写卡")
    parser.add_argument("--serial", help="ADB USB serial；多台设备时必须指定")
    parser.add_argument("--request-stdin", action="store_true", help="从 stdin 读取 JSON 请求")
    parser.add_argument("--yes", action="store_true", help="自动化模式下确认当前 EID")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("scan", help="读取卡片状态")
    sub.add_parser("download", help="下载并安装运营商配置")
    sub.add_parser("notify", help="处理卡上待发送的运营商通知")
    args = parser.parse_args(argv)
    try:
        if args.command == "scan":
            return command_scan(args)
        if args.command == "download":
            return command_download(args)
        return command_notify(args)
    except (ComputerError, subprocess.SubprocessError):
        print("操作未完成：请重新读取卡片核对状态后再决定下一步。", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
