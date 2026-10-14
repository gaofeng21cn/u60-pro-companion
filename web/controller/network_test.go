package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeExecutable(t *testing.T, path, body string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(body), 0700); err != nil {
		t.Fatal(err)
	}
}

func TestSaveNetworkDisableDoesNotRequireOptionalDependencies(t *testing.T) {
	a := testApp(t)
	panel := t.TempDir()
	a.panelDir = panel
	log := filepath.Join(panel, "calls")
	manifest := filepath.Join(panel, "network-control", "manifest")
	if err := os.MkdirAll(filepath.Dir(manifest), 0700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(manifest, []byte("udp=1 guard6=1\n"), 0600); err != nil {
		t.Fatal(err)
	}
	writeExecutable(t, filepath.Join(panel, "network-control.sh"), "#!/bin/sh\necho \"$1\" >> '"+log+"'\ncase \"$1\" in\n status) printf '%s\\n' '{\"ok\":true,\"available\":false,\"iptables_ready\":false,\"tproxy_supported\":false,\"ipv6_guard_supported\":false}' ;;\n off) rm -f '"+manifest+"' '"+filepath.Join(panel, "network-control", "resources-owned")+"'; printf '%s\\n' '{\"ok\":true,\"active\":false}' ;;\n *) printf '%s\\n' '{\"ok\":false,\"error\":\"unexpected\"}'; exit 1 ;;\nesac\n")
	old, _, err := a.config()
	if err != nil {
		t.Fatal(err)
	}
	result := a.saveNetwork(M{
		"revision": sha(old), "enabled": false, "udp": true, "guard6": true,
		"clients": []any{M{"mac": "aa:bb:cc:dd:ee:ff", "target": "Old"}},
	})
	if !boolv(result["ok"]) {
		t.Fatal(result)
	}
	calls, err := os.ReadFile(log)
	if err != nil {
		t.Fatal(err)
	}
	if got := strings.TrimSpace(string(calls)); got != "status\noff" {
		t.Fatalf("disable calls = %q; pause/apply must not run", got)
	}
	if _, err := os.Stat(manifest); !os.IsNotExist(err) {
		t.Fatalf("manifest still exists: %v", err)
	}
	_, root, err := a.config()
	if err != nil {
		t.Fatal(err)
	}
	policy, err := policyFrom(root)
	if err != nil || policy.Enabled || policy.UDP || policy.Guard6 || len(policy.Clients) != 0 {
		t.Fatalf("disabled policy = %#v, err=%v", policy, err)
	}
}

func TestSaveNetworkEnableRequiresMihomoReady(t *testing.T) {
	a := testApp(t)
	panel := t.TempDir()
	a.panelDir = panel
	log := filepath.Join(panel, "calls")
	writeExecutable(t, filepath.Join(panel, "network-control.sh"), "#!/bin/sh\necho \"$1\" >> '"+log+"'\n[ \"$1\" = status ] || exit 1\nprintf '%s\\n' '{\"ok\":true,\"available\":true,\"mihomo_ready\":false,\"iptables_ready\":true,\"tproxy_supported\":true,\"ipv6_guard_supported\":true}'\n")
	old, _, err := a.config()
	if err != nil {
		t.Fatal(err)
	}
	result := a.saveNetwork(M{
		"revision": sha(old), "enabled": true, "udp": false, "guard6": false,
		"clients": []any{},
	})
	if boolv(result["ok"]) || !strings.Contains(text(result["message"]), "Mihomo") {
		t.Fatalf("enable without Mihomo should be rejected: %v", result)
	}
	calls, err := os.ReadFile(log)
	if err != nil {
		t.Fatal(err)
	}
	if got := strings.TrimSpace(string(calls)); got != "status" {
		t.Fatalf("unexpected calls after rejected enable: %q", got)
	}
}
