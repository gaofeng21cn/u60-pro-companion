#!/usr/bin/env python3
"""Host-only connector acceptance using a copied package and synthetic ADB."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "integrations/opl-u60-connector"
DIGEST = hashlib.sha256(b"u60-imei-v1:000000000000001").hexdigest()

with tempfile.TemporaryDirectory(prefix="u60-agent-") as directory:
    tmp = pathlib.Path(directory)
    package = tmp / "installed-package"
    shutil.copytree(PACKAGE, package)
    binary = tmp / "panel-control"
    subprocess.run(["cc", "-O1", "-I", str(ROOT / "panel/vendor"), str(ROOT / "panel/panel-control.c"),
                    str(ROOT / "panel/vendor/cJSON.c"), "-lm", "-o", str(binary)], check=True)
    fixture = tmp / "device.json"
    fixture.write_text(json.dumps({
        "wifi.zte_mbb": {"wifi_onoff": "1", "secret": "DO_NOT_EXPOSE"},
        "wifi.main_2g": {"ssid": "PRIVATE_SSID", "disabled": "0", "key": "DO_NOT_EXPOSE"},
        "zwrt_router.api.router_get_dhcp_router": {"lan_addr": "192.168.8.1", "lan_netmask": "255.255.255.0"},
        "zte_nwinfo_api.nwinfo_get_netinfo": {"network_type": "SA", "signalbar": "5"},
        "zwrt_bsp.battery.list": {"battery_capacity": 84}, "ts.prefs": {}, "ts.status": {},
    }))
    state = subprocess.run([str(binary), "--fixture", str(fixture)], input='{"action":"state","args":{}}',
                           text=True, capture_output=True, check=True)
    source = json.loads(state.stdout)
    assert source["fixture_write_count"] == 0
    (tmp / "state.json").write_text(state.stdout)
    (tmp / "diagnostics.json").write_text(json.dumps({"ok": True, "report": {"checks": [
        {"id": "route", "status": "passed", "detail": "PRIVATE_ROUTE"},
        {"id": "https.default.0", "status": "failed", "http_status": 0, "timed_out": True},
        {"id": "clash.listener", "status": "skipped", "secret": "DO_NOT_EXPOSE"}
    ]}}))
    fake = tmp / "adb"
    fake.write_text("""#!/usr/bin/env python3
import json, os, pathlib, sys
p=pathlib.Path(os.environ['FAKE_ROOT']); args=sys.argv[1:]
with (p/'calls').open('a') as f: f.write(json.dumps(args)+'\\n')
mode=os.environ.get('FAKE_MODE','')
if args==['devices']:
 print('List of devices attached\\nSYNTHETIC\\tdevice'+('\\nSECOND\\tdevice' if mode=='multi' else ''));sys.exit()
assert args[:3]==['-s','SYNTHETIC','exec-out']
if args[3:5]==['sh','-c']:
 assert 'u60-imei-v1:' in args[5] and 'unset imei' in args[5]
 calls=sum('u60-imei-v1:' in line for line in (p/'calls').read_text().splitlines())
 print('UNKNOWN' if mode=='firmware' else 'BD_CNMU5250V1.0.0B31')
 print('5.15.194-perf')
 print('0'*64 if mode=='swap' and calls>1 else os.environ['FAKE_DIGEST']);sys.exit()
assert args[3:]==['/data/u60-panel/panel-control']
request=json.load(sys.stdin);assert request['args']=={};assert request['action'] in ['state','diag.network']
if mode=='disconnect': print('DO_NOT_EXPOSE',file=sys.stderr);sys.exit(1)
if mode=='malformed': print('{oops');sys.exit()
if mode=='nan': print('{"ok":true,"data":{"battery":NaN}}');sys.exit()
if mode=='owner_failure': print('{"ok":false,"message":"DO_NOT_EXPOSE"}');sys.exit()
print((p/('state.json' if request['action']=='state' else 'diagnostics.json')).read_text())
""")
    fake.chmod(0o700)
    env = {**os.environ, "U60_ADB": str(fake), "FAKE_ROOT": str(tmp), "FAKE_DIGEST": DIGEST}
    env.pop("U60_ADB_SERIAL", None)
    env.pop("U60_TARGET_IDENTITY_SHA256", None)
    count = 0

    def call(operation, request=None, mode="", extra=None):
        global count
        (tmp / "calls").write_text("")
        output = subprocess.run(["python3", "connector.py", operation], cwd=package,
                                input=json.dumps(request or {}), env={**env, "FAKE_MODE": mode, **(extra or {})},
                                text=True, capture_output=True, timeout=10, check=True)
        assert not output.stderr, output.stderr
        assert all(secret not in output.stdout for secret in ("DO_NOT_EXPOSE", "PRIVATE_SSID", "PRIVATE_ROUTE", "SYNTHETIC"))
        count += 1
        return json.loads(output.stdout), (tmp / "calls").read_text().splitlines()

    result, calls = call("describe")
    assert result["data"]["read_only"] and not calls
    result, calls = call("status")
    assert result["state"] == "ready" and len(calls) == 4
    data = result["data"]
    assert data["battery"] == 84 and data["device"]["identity_sha256"] == DIGEST
    assert data["freshness"]["state"] == "fresh" and data["freshness"]["max_age_seconds"] == 30
    assert data["handoff"]["url"] == "http://192.168.8.1/index.html#u60_enhanced"
    assert data["handoff"]["device_identity_sha256"] == DIGEST
    assert data["revision"].startswith("sha256:") and data["observed_at"]
    assert data["items"][0]["observed_at"] == data["observed_at"]
    assert data["items"][0]["device"] == data["device"] and data["items"][0]["handoff"] == data["handoff"]
    result, calls = call("diagnostics", {"expected_identity_sha256": DIGEST})
    assert result["state"] == "ready" and not result["data"]["network_checks_passed"]
    assert [c["status"] for c in result["data"]["checks"]] == ["passed", "failed", "skipped"]
    result, _ = call("diagnostics", extra={"U60_TARGET_IDENTITY_SHA256": DIGEST})
    assert result["state"] == "ready"
    for operation, request, reason in [
        ("system.power", {}, "unknown_operation"),
        ("status", {"action": "wifi.power"}, "invalid_input"),
        ("status", {"expected_identity_sha256": ""}, "invalid_identity"),
        ("status", {"expected_identity_sha256": "bad"}, "invalid_identity"),
    ]:
        result, calls = call(operation, request)
        assert result == {"state": "unavailable", "reason": reason} and not calls
    result, calls = call("diagnostics")
    assert result["state"] == "input_required" and result["reason"] == "device_identity_required" and not calls
    result, calls = call("status", {"expected_identity_sha256": "0" * 64})
    assert result["reason"] == "device_identity_changed" and len(calls) == 2
    for mode, reason in [("multi", "exactly_one_device_required"), ("swap", "device_identity_changed"),
                         ("firmware", "unsupported_firmware"), ("disconnect", "owner_unavailable"),
                         ("malformed", "invalid_owner_response"), ("owner_failure", "owner_read_failed"),
                         ("nan", "invalid_owner_response")]:
        result, _ = call("status", mode=mode)
        assert result == {"state": "unavailable", "reason": reason}, result
    result, _ = call("status", mode="multi", extra={"U60_ADB_SERIAL": "SYNTHETIC"})
    assert result["state"] == "ready"
    descriptor = json.loads((package / "opl-package.json").read_text())
    adapter = json.loads((package / "app-contribution-adapter.json").read_text())
    assert not adapter["actions"] and not descriptor["app_contributions"]["commands"]
    assert descriptor["publication_channel_admission"] == "development_only"
    for view in descriptor["app_contributions"]["views"]:
        entry = adapter["reads"][view["data_ref"]]
        assert entry["stdin"] == "input_json" and (package / entry["argv"][1]).is_file()
    digest = hashlib.sha256()
    for name in descriptor["content_lock"]["paths"]:
        name_bytes = name.encode(); content = (package / name).read_bytes()
        digest.update(len(name_bytes).to_bytes(8, "big")); digest.update(name_bytes)
        digest.update(len(content).to_bytes(8, "big")); digest.update(content)
    assert descriptor["content_lock"]["digest"] == "sha256:" + digest.hexdigest()
    framework = os.environ.get("U60_FRAMEWORK_ROOT")
    if framework:
        script = """
        import fs from 'node:fs';
        const root = process.env.U60_FRAMEWORK_ROOT;
        const {default: Ajv} = await import(root + '/node_modules/ajv/dist/2020.js');
        const schema = JSON.parse(fs.readFileSync(root + '/contracts/opl-framework/capability-package-manifest.schema.json'));
        const manifest = JSON.parse(fs.readFileSync('opl-package.json'));
        const validate = new Ajv({strict: false}).compile(schema);
        if (!validate(manifest)) throw new Error(JSON.stringify(validate.errors));
        const {executeDeclarativeAppContribution} = await import(root + '/src/read-models/operator/app-contribution-adapter.ts');
        const request = {schema_version: 'opl-package-app-contribution-request.v1', operation: 'read',
          ref: 'u60.companion.v1#status', input: {expected_identity_sha256: process.env.FAKE_DIGEST}};
        const status = executeDeclarativeAppContribution({packageRoot: process.cwd(), request});
        if (!status.ok || status.result.state !== 'ready' || status.result.data.device.identity_sha256 !== process.env.FAKE_DIGEST)
          throw new Error(JSON.stringify(status));
        const {renderOwnerView} = await import(root + '/src/mcp-app/presentation.js');
        const rendered = renderOwnerView(manifest.app_contributions.views[0], status.result.data.items[0]);
        if (!rendered.includes(process.env.FAKE_DIGEST) || !rendered.includes(status.result.data.observed_at)
            || !rendered.includes('192.168.8.1')) throw new Error('Shared view lost device context');
        request.ref = 'u60.companion.v1#diagnostics'; request.input = {};
        process.env.U60_TARGET_IDENTITY_SHA256 = process.env.FAKE_DIGEST;
        const diagnostics = executeDeclarativeAppContribution({packageRoot: process.cwd(), request});
        if (!diagnostics.ok || diagnostics.result.state !== 'ready' || diagnostics.result.data.network_checks_passed !== false)
          throw new Error(JSON.stringify(diagnostics));
        console.log('PASS: Framework manifest schema, shared adapter and device-bound presentation');
        """
        (tmp / "calls").write_text("")
        subprocess.run(["bun", "--eval", script], cwd=package,
                       env={**env, "U60_FRAMEWORK_ROOT": framework}, check=True, timeout=20)
    print(f"PASS: {count} isolated connector scenarios; actual panel-control state fixture; no device access")
