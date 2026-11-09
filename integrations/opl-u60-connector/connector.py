#!/usr/bin/env python3
"""Read-only host adapter for the installed U60 panel-control owner."""
import datetime
import hashlib
import ipaddress
import json
import os
import re
import subprocess
import sys
import time

FIRMWARE = {
    "BD_FLYMODEMMU5250V1.0.0B28": "5.15.185-perf",
    "BD_CNMU5250V1.0.0B31": "5.15.194-perf",
}
# Fixed command; the IMEI never crosses the ADB stdout boundary.
IDENTITY_COMMAND = """set -e
fw=$(ubus -t 5 call zwrt_zte_mdm.api get_zwrt_common_info '{}' | jsonfilter -e '@.wa_inner_version')
imei=$(ubus -t 5 call zwrt_web device_info '{}' | jsonfilter -e '@.imei')
case "$imei" in ''|*[!0-9]*) exit 1;; esac
[ "${#imei}" = 15 ] || exit 1
digest=$(printf 'u60-imei-v1:%s' "$imei" | sha256sum | cut -d ' ' -f 1)
unset imei
printf '%s\n%s\n%s\n' "$fw" "$(uname -r)" "$digest"
"""
STATUS_KEYS = (
    "wifi_status", "tailscale_status", "usb_status", "network_profile", "network",
    "band", "signal", "battery", "temperature", "clients", "today_bytes",
    "month_bytes", "download_bps", "upload_bps", "uptime_seconds", "cpu_percent",
    "memory_used_percent",
)
READS = {"status": "state", "diagnostics": "diag.network"}


class Unavailable(Exception):
    pass


def parse_json(text):
    def reject_constant(_):
        raise ValueError("non-finite JSON number")
    return json.loads(text, parse_constant=reject_constant)


def run(argv, *, input=None, timeout=15):
    try:
        result = subprocess.run(argv, input=input, text=True, capture_output=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        raise Unavailable("transport_unavailable") from None
    if result.returncode:
        raise Unavailable("owner_unavailable")
    if len(result.stdout) > 512_000:
        raise Unavailable("invalid_owner_response")
    return result.stdout


def device(adb):
    output = run([adb, "devices"])
    devices = [line.split()[0] for line in output.splitlines()[1:]
               if len(line.split()) == 2 and line.split()[1] == "device"]
    selected = os.environ.get("U60_ADB_SERIAL")
    if selected:
        if selected not in devices:
            raise Unavailable("selected_device_unavailable")
    elif len(devices) == 1:
        selected = devices[0]
    else:
        raise Unavailable("exactly_one_device_required")
    return [adb, "-s", selected, "exec-out"]


def identity(route):
    parts = run(route + ["sh", "-c", IDENTITY_COMMAND]).strip().splitlines()
    if len(parts) != 3 or parts[0] not in FIRMWARE or FIRMWARE[parts[0]] != parts[1]:
        raise Unavailable("unsupported_firmware")
    if not re.fullmatch(r"[0-9a-f]{64}", parts[2]):
        raise Unavailable("device_identity_unavailable")
    return {"identity_sha256": parts[2], "firmware": parts[0], "kernel": parts[1]}


def webpage(raw):
    # Only the owner-reported local management address, never a caller-supplied URL.
    sections = raw.get("sections", [])
    if not isinstance(sections, list):
        raise Unavailable("invalid_owner_response")
    for section in sections:
        if not isinstance(section, dict) or section.get("id") != "router":
            continue
        items = section.get("items", [])
        if not isinstance(items, list):
            raise Unavailable("invalid_owner_response")
        for item in items:
            if not isinstance(item, dict) or item.get("id") != "lan_ip":
                continue
            try:
                address = ipaddress.IPv4Address(item.get("value"))
            except (ValueError, TypeError):
                continue
            if any(address in ipaddress.IPv4Network(cidr) for cidr in
                   ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")):
                return {"url": f"http://{address}/index.html#u60_enhanced",
                        "authentication": "factory_login_required",
                        "device_identity_sha256": None}
    return None


def projection(operation, raw, target):
    if operation == "status":
        values = raw.get("data")
        if not isinstance(values, dict):
            raise Unavailable("invalid_owner_response")
        data = {key: value for key, value in values.items() if key in STATUS_KEYS
                and (value is None or type(value) in (str, int, float, bool))}
        handoff = webpage(raw)
        if handoff:
            handoff["device_identity_sha256"] = target["identity_sha256"]
            data["handoff"] = handoff
        data["items"] = [{"id": target["identity_sha256"], "title": "U60 Pro Companion",
                          "status": "observed", "summary": " · ".join(str(data.get(key) or "未知")
                          for key in ("network", "wifi_status", "usb_status"))}]
        return data
    report = raw.get("report")
    if not isinstance(report, dict) or not isinstance(report.get("checks"), list) or not report["checks"]:
        raise Unavailable("diagnostics_unavailable")
    checks = []
    for check in report["checks"]:
        if not isinstance(check, dict) or check.get("status") not in ("passed", "failed", "skipped"):
            raise Unavailable("invalid_owner_response")
        if not isinstance(check.get("id"), str) or not re.fullmatch(r"[a-z0-9_.-]{1,80}", check["id"]):
            raise Unavailable("invalid_owner_response")
        clean = {"id": check["id"], "status": check["status"]}
        for key in ("curl_exit_code", "http_status", "ssl_verify_result", "seconds", "errno", "timed_out", "resolver_code", "measured"):
            if type(check.get(key)) in (int, float, bool):
                clean[key] = check[key]
        checks.append(clean)
    return {"checks": checks, "network_checks_passed": all(c["status"] == "passed" for c in checks),
            "scope": "device_ipv4_and_existing_clash_http; not hotspot_clients_or_full_ipv6_udp",
            "items": [{**c, "title": c["id"]} for c in checks]}


def contribute(operation, request):
    if operation == "describe":
        if request:
            raise Unavailable("invalid_input")
        return {"state": "ready", "data": {"owner": "u60-pro-companion/panel-control",
                "operations": ["status", "diagnostics"], "read_only": True,
                "diagnostics_requires_identity": True, "device_transport": "authorized_local_adb"}}
    if operation not in READS:
        raise Unavailable("unknown_operation")
    if not isinstance(request, dict) or set(request) - {"expected_identity_sha256"}:
        raise Unavailable("invalid_input")
    expected = request.get("expected_identity_sha256", os.environ.get("U60_TARGET_IDENTITY_SHA256"))
    if expected is not None and (not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected)):
        raise Unavailable("invalid_identity")
    if operation == "diagnostics" and expected is None:
        return {"state": "input_required", "reason": "device_identity_required",
                "missing_fields": ["expected_identity_sha256"], "input_schema": {
                    "type": "object", "properties": {"expected_identity_sha256": {
                        "type": "string", "pattern": "^[0-9a-f]{64}$"}},
                    "required": ["expected_identity_sha256"], "additionalProperties": False}}
    started = time.monotonic()
    route = device(os.environ.get("U60_ADB", "adb"))
    target = identity(route)
    if expected and expected != target["identity_sha256"]:
        raise Unavailable("device_identity_changed")
    text = run(route + ["/data/u60-panel/panel-control"],
               input=json.dumps({"action": READS[operation], "args": {}}),
               timeout=55 if operation == "diagnostics" else 30)
    try:
        raw = parse_json(text)
    except (ValueError, TypeError):
        raise Unavailable("invalid_owner_response") from None
    if not isinstance(raw, dict) or raw.get("ok") is not True:
        raise Unavailable("owner_read_failed")
    if identity(route) != target:
        raise Unavailable("device_identity_changed")
    data = projection(operation, raw, target)
    revision = "sha256:" + hashlib.sha256(json.dumps({"device": target, "data": data}, sort_keys=True,
                                                     ensure_ascii=False).encode()).hexdigest()
    observed = datetime.datetime.now(datetime.timezone.utc).isoformat()
    metadata = {"device": target, "revision": revision, "observed_at": observed,
                "freshness": {"state": "fresh", "max_age_seconds": 30,
                              "sample_duration_seconds": round(time.monotonic() - started, 3)},
                "owner": "u60-pro-companion/panel-control", "read_only": True}
    # Shared collection renderers display the selected item, not sibling fields.
    if operation == "status":
        data["items"][0].update({key: value for key, value in data.items() if key != "items"})
    for item in data["items"]:
        item.update(metadata)
    return {"state": "ready", "data": {**data, **metadata}}


def main():
    try:
        raw = sys.stdin.read(8193)
        if len(raw) > 8192:
            raise Unavailable("invalid_input")
        value = parse_json(raw) if raw.strip() else {}
        if not isinstance(value, dict):
            raise Unavailable("invalid_input")
        result = contribute(sys.argv[1] if len(sys.argv) == 2 else "", value)
    except Unavailable as error:
        result = {"state": "unavailable", "reason": str(error)}
    except (ValueError, TypeError, KeyError):
        result = {"state": "unavailable", "reason": "invalid_input"}
    print(json.dumps(result, ensure_ascii=False, allow_nan=False))


if __name__ == "__main__":
    main()
