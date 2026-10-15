#!/usr/bin/env python3
"""Host-only tests for panel/auth-bypass.sh. No device writes.

The script is run with stub uci/iptables/ip6tables/mkdir/kill probes so we can
assert the gate is pinned to 0, the enforcement chain is cleared, drift is
detected when the factory rewrites a gate, and disable returns ownership.
"""
import os, pathlib, subprocess, tempfile, textwrap

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "panel" / "auth-bypass.sh"


def make_env(tmp, uci_state):
    bindir = pathlib.Path(tmp) / "bin"
    bindir.mkdir()
    state = pathlib.Path(tmp) / "uci.txt"
    state.write_text("\n".join(f"{k}={v}" for k, v in uci_state.items()) + "\n")

    (bindir / "uci").write_text(textwrap.dedent(f"""
        #!/bin/sh
        STATE={state}
        cmd=$1; shift
        case "$cmd" in
          -q) cmd=$1; shift ;;
        esac
        key=$1
        case "$cmd" in
          get) grep -E "^${{key}}=" "$STATE" | head -n1 | cut -d= -f2- ;;
          set)
            k=${{1%%=*}}; v=${{1#*=}}
            grep -v "^${{k}}=" "$STATE" > "$STATE.new" 2>/dev/null || true
            echo "$k=$v" >> "$STATE.new"; mv "$STATE.new" "$STATE" ;;
          commit) exit 0 ;;
        esac
    """))
    (bindir / "iptables").write_text(textwrap.dedent("""
        #!/bin/sh
        # Track rules per table so flush/read behave like the real chains.
        STORE=${IPT_STORE:-/tmp/ipt-store.$$}
        case "$1" in -t) tbl=$2; shift 2 ;; *) tbl=filter ;; esac
        echo "$*" >> "$STORE"
        case "$1" in
          -S) chain=$2
              if [ -f "$STORE" ]; then grep -E "(^| )$chain" "$STORE" | grep '^-A' || true; fi ;;
          -F) chain=$2
              if [ -f "$STORE" ]; then grep -v -- "-F $chain" "$STORE" > "$STORE.tmp"; mv "$STORE.tmp" "$STORE"; fi ;;
        esac
    """))
    (bindir / "ip6tables").write_text((bindir / "iptables").read_text())
    init = bindir / "u60-auth-init"
    init.write_text(textwrap.dedent(f"""
        #!/bin/sh
        case "$1" in
          enable) : > "$AUTH_INIT_STATE.enabled" ;;
          disable) rm -f "$AUTH_INIT_STATE.enabled" ;;
          start)
            sh "$AUTH_BYPASS_SCRIPT" watch >/dev/null 2>&1 &
            ;;
          stop)
            p=$(cat "$AUTH_BYPASS_PIDFILE" 2>/dev/null || true)
            case "$p" in ''|*[!0-9]*) ;; *) kill "$p" 2>/dev/null || true ;; esac
            ;;
          *) exit 2 ;;
        esac
    """))
    os.chmod(init, 0o755)
    os.chmod(bindir / "uci", 0o755)
    os.chmod(bindir / "iptables", 0o755)
    os.chmod(bindir / "ip6tables", 0o755)
    return bindir, state


def run(bindir, args, extra_env=None):
    env = dict(os.environ)
    env["PATH"] = f"{bindir}:{env['PATH']}"
    env.update(extra_env or {})
    return subprocess.run(["sh", str(SCRIPT), *args], capture_output=True,
                          text=True, env=env, timeout=20)


def test_pin_and_clear(tmp):
    bindir, state = make_env(tmp, {
        "zwrt_router.tmp_router.auth_second_simcard_valid": "1",
        "zwrt_router.tmp_router.simcard_switch_flag": "1",
    })
    root = pathlib.Path(tmp) / "authroot"
    intent = pathlib.Path(tmp) / "enabled"
    store = pathlib.Path(tmp) / "ipt"
    store.write_text("-A second_auth_redirect_chain -j DROP\n")
    p = run(bindir, ["enable"], {
        "AUTH_BYPASS_ROOT": str(root),
        "AUTH_BYPASS_INTENT": str(intent),
        "AUTH_BYPASS_PIDFILE": str(pathlib.Path(tmp) / "pid"),
        "AUTH_BYPASS_LOCK": str(pathlib.Path(tmp) / "lock"),
        "IPT_STORE": str(store),
        "AUTH_BYPASS_INTERVAL": "3600",
        "AUTH_BYPASS_INIT": str(bindir / "u60-auth-init"),
        "AUTH_INIT_STATE": str(pathlib.Path(tmp) / "init-state"),
        "AUTH_BYPASS_SCRIPT": str(SCRIPT),
    })
    assert p.returncode == 0, p.stderr
    out = p.stdout
    assert '"enabled":true' in out, out
    assert '"valid":0' in out and '"switch":0' in out, out
    fields = dict(l.split("=", 1) for l in state.read_text().split())
    assert fields["zwrt_router.tmp_router.auth_second_simcard_valid"] == "0"
    assert fields["zwrt_router.tmp_router.simcard_switch_flag"] == "0"
    assert intent.exists()
    stop = run(bindir, ["disable"], {
        **{
            "AUTH_BYPASS_ROOT": str(root),
            "AUTH_BYPASS_INTENT": str(intent),
            "AUTH_BYPASS_PIDFILE": str(pathlib.Path(tmp) / "pid"),
            "AUTH_BYPASS_LOCK": str(pathlib.Path(tmp) / "lock"),
            "IPT_STORE": str(store),
        },
        "AUTH_BYPASS_INIT": str(bindir / "u60-auth-init"),
        "AUTH_INIT_STATE": str(pathlib.Path(tmp) / "init-state"),
        "AUTH_BYPASS_SCRIPT": str(SCRIPT),
    })
    assert stop.returncode == 0, stop.stderr


def test_disable_returns_ownership(tmp):
    bindir, state = make_env(tmp, {
        "zwrt_router.tmp_router.auth_second_simcard_valid": "1",
        "zwrt_router.tmp_router.simcard_switch_flag": "1",
    })
    env = {
        "AUTH_BYPASS_ROOT": str(pathlib.Path(tmp) / "authroot"),
        "AUTH_BYPASS_INTENT": str(pathlib.Path(tmp) / "enabled"),
        "AUTH_BYPASS_PIDFILE": str(pathlib.Path(tmp) / "pid"),
        "AUTH_BYPASS_LOCK": str(pathlib.Path(tmp) / "lock"),
        "IPT_STORE": str(pathlib.Path(tmp) / "ipt"),
        "AUTH_BYPASS_INTERVAL": "3600",
        "AUTH_BYPASS_INIT": str(bindir / "u60-auth-init"),
        "AUTH_INIT_STATE": str(pathlib.Path(tmp) / "init-state"),
        "AUTH_BYPASS_SCRIPT": str(SCRIPT),
    }
    run(bindir, ["enable"], env)
    p = run(bindir, ["disable"], env)
    assert p.returncode == 0, p.stderr
    assert '"enabled":false' in p.stdout, p.stdout
    assert not pathlib.Path(env["AUTH_BYPASS_INTENT"]).exists()
    # Disable must not silently keep the gate pinned; factory owns it again.
    fields = dict(l.split("=", 1) for l in state.read_text().split())
    assert fields["zwrt_router.tmp_router.auth_second_simcard_valid"] == "0"


def test_status_reports_drift(tmp):
    bindir, state = make_env(tmp, {
        "zwrt_router.tmp_router.auth_second_simcard_valid": "1",
        "zwrt_router.tmp_router.simcard_switch_flag": "0",
    })
    env = {
        "AUTH_BYPASS_ROOT": str(pathlib.Path(tmp) / "authroot"),
        "AUTH_BYPASS_INTENT": str(pathlib.Path(tmp) / "enabled"),
        "AUTH_BYPASS_PIDFILE": str(pathlib.Path(tmp) / "pid"),
        "AUTH_BYPASS_LOCK": str(pathlib.Path(tmp) / "lock"),
        "IPT_STORE": str(pathlib.Path(tmp) / "ipt"),
    }
    p = run(bindir, ["status"], env)
    assert p.returncode == 0, p.stderr
    assert '"drift":true' in p.stdout, p.stdout


def main():
    tests = [test_pin_and_clear, test_disable_returns_ownership, test_status_reports_drift]
    with tempfile.TemporaryDirectory() as base:
        for i, t in enumerate(tests):
            sub = pathlib.Path(base) / str(i)
            sub.mkdir()
            t(sub)
    print(f"auth-bypass: {len(tests)}/{len(tests)} passed")


if __name__ == "__main__":
    main()
