package main

// Adapted integration patterns: xiaohaiguan 80096fe6 (Mihomo listeners +
// MAC dispatch) and GodBlessAmerica fd4f211c (persistent MAC identity).
// See docs/UPSTREAM-LEARNING.md for deferred WireGuard research.
import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"os"
	"os/exec"
	"strings"
	"time"

	"gopkg.in/yaml.v3"
)

type devicePolicy struct {
	MAC    string `json:"mac"`
	Target string `json:"target"`
}
type lanPolicy struct {
	Enabled bool           `json:"enabled"`
	UDP     bool           `json:"udp"`
	Guard6  bool           `json:"guard6"`
	Clients []devicePolicy `json:"clients"`
}

func (a *App) networkScript(command, input string) (M, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 25*time.Second)
	defer cancel()
	c := exec.CommandContext(ctx, a.panelPath("network-control.sh"), command)
	c.Stdin = strings.NewReader(input)
	b, e := c.Output()
	var r M
	if len(b) > 65536 || json.Unmarshal(b, &r) != nil {
		return nil, errors.New("网络策略控制器未返回有效结果")
	}
	if e != nil || !boolv(r["ok"]) {
		return r, errors.New("网络策略未确认：" + text(r["error"]))
	}
	return r, nil
}
func policyFrom(root *yaml.Node) (lanPolicy, error) {
	var p lanPolicy
	n := named(root, "u60-network-policy")
	if n == nil {
		return p, nil
	}
	b, e := yaml.Marshal(n)
	if e != nil {
		return p, e
	}
	var value any
	if e = yaml.Unmarshal(b, &value); e != nil {
		return p, e
	}
	data, e := json.Marshal(value)
	if e != nil {
		return p, e
	}
	if bytes.Equal(data, []byte("null")) {
		return p, nil
	}
	e = json.Unmarshal(data, &p)
	// Older candidates had no explicit master switch. Treat a non-empty
	// legacy policy as enabled while keeping an empty/default policy disabled.
	if !p.Enabled && (p.UDP || p.Guard6 || len(p.Clients) > 0) {
		p.Enabled = true
	}
	return p, e
}
func (a *App) networkState() M {
	old, root, e := a.config()
	if e != nil {
		return fail("请先初始化 Mihomo 配置")
	}
	p, e := policyFrom(root)
	if e != nil {
		return fail("保存的终端策略无法解析")
	}
	out := success("终端网络状态已读取")
	out["revision"] = sha(old)
	out["policy"] = p
	out["profile"] = a.networkProfile()
	targets := []any{"DIRECT"}
	seen := map[string]bool{"DIRECT": true}
	for _, key := range []string{"proxies", "proxy-groups"} {
		if n := named(root, key); n != nil {
			for _, v := range n.Content {
				name := nstr(v, "name")
				if name != "" && !seen[name] {
					targets = append(targets, name)
					seen[name] = true
				}
			}
		}
	}
	// Provider nodes can be selected while online; config-backed groups remain
	// usable after restart even before the provider cache has loaded.
	if live, err := a.api("GET", "/proxies", nil); err == nil {
		for name, v := range obj(live["proxies"]) {
			if !seen[name] && name != "GLOBAL" && !providerMetaName(name) && !strings.HasPrefix(name, "REJECT") {
				targets = append(targets, name)
				seen[name] = true
			}
			_ = v
		}
	}
	out["targets"] = targets
	for _, command := range []string{"status", "clients"} {
		r, err := a.networkScript(command, "")
		if err != nil {
			out[command] = M{"available": false}
		} else {
			out[command] = r
		}
	}
	// Active core connections are observation, not a synthetic UDP success probe.
	udpConnections := 0
	var udpBytes float64
	observed := false
	if r, err := a.api("GET", "/connections", nil); err == nil {
		observed = true
		for _, v := range arr(r["connections"]) {
			c := obj(v)
			if strings.EqualFold(text(obj(c["metadata"])["network"]), "udp") {
				udpConnections++
				for _, k := range []string{"upload", "download"} {
					if n, ok := c[k].(float64); ok {
						udpBytes += n
					}
				}
			}
		}
	}
	out["udp_observation"] = M{"available": observed, "connections": udpConnections, "bytes": udpBytes, "sampled_at": time.Now().Unix(), "scope": "active_core_connections"}
	return out
}
func yamlSeq(root *yaml.Node, key string) *yaml.Node {
	n := named(root, key)
	if n == nil {
		n = &yaml.Node{Kind: yaml.SequenceNode, Tag: "!!seq"}
		nset(root, key, []any{})
		n = named(root, key)
	}
	return n
}
func keepNodes(n *yaml.Node, remove func(*yaml.Node) bool) {
	kept := []*yaml.Node{}
	for _, v := range n.Content {
		if !remove(v) {
			kept = append(kept, v)
		}
	}
	n.Content = kept
}
func appendNode(n *yaml.Node, value any) {
	v := &yaml.Node{}
	v.Encode(value)
	n.Content = append(n.Content, v)
}

// New top-level blocks are appended; existing blocks use the established
// byte-preserving rewriter so controller secrets and unrelated blocks survive.
func networkBlocks(old []byte, root *yaml.Node, keys []string) ([]byte, error) {
	var original yaml.Node
	if yaml.Unmarshal(old, &original) != nil {
		return nil, errors.New("配置解析失败")
	}
	existing := []string{}
	added := []string{}
	for _, k := range keys {
		if named(original.Content[0], k) == nil {
			added = append(added, k)
		} else {
			existing = append(existing, k)
		}
	}
	next := append([]byte(nil), old...)
	var e error
	if len(existing) > 0 {
		next, e = replaceBlocks(old, root, existing)
		if e != nil {
			return nil, e
		}
	}
	for _, k := range added {
		w := &yaml.Node{Kind: yaml.MappingNode, Tag: "!!map", Content: []*yaml.Node{{Kind: yaml.ScalarNode, Tag: "!!str", Value: k}, named(root, k)}}
		b, e := yaml.Marshal(w)
		if e != nil {
			return nil, e
		}
		if len(next) > 0 && next[len(next)-1] != '\n' {
			next = append(next, '\n')
		}
		next = append(next, b...)
	}
	return next, nil
}
func macCanonical(s string) (string, error) {
	m, e := net.ParseMAC(s)
	if e != nil || len(m) != 6 || m[0]&1 != 0 {
		return "", errors.New("MAC 地址无效")
	}
	return m.String(), nil
}
func (a *App) saveNetwork(args M) M {
	old, root, e := a.config()
	if e != nil {
		return fail("请先初始化 Mihomo 配置")
	}
	if text(args["revision"]) != sha(old) {
		return fail("配置已变化，请刷新后重试")
	}
	_, uok := args["udp"].(bool)
	_, gok := args["guard6"].(bool)
	rows, ok := args["clients"].([]any)
	_, eok := args["enabled"].(bool)
	if !ok || len(rows) > 32 || !uok || !gok || !eok {
		return fail("需要明确的模块开关、UDP、IPv6 开关及最多 32 个终端策略")
	}
	enabled := boolv(args["enabled"])
	status, e := a.networkScript("status", "")
	if enabled && (e != nil || !boolv(status["available"])) {
		return fail("终端网络模块不可用，未修改配置")
	}
	if enabled && !boolv(status["mihomo_ready"]) {
		return fail("Mihomo 核心未就绪，未修改配置")
	}
	if !enabled {
		// A disabled module carries no hidden per-device policy. The UI may send
		// the previous rows while turning the master switch off; discard them in
		// the same transaction so re-enabling starts from an explicit choice.
		rows = []any{}
	}
	if enabled && len(rows) > 0 && !boolv(status["iptables_ready"]) {
		return fail("iptables 不可用，未修改配置")
	}
	if enabled && boolv(args["udp"]) && !boolv(status["tproxy_supported"]) {
		return fail("设备不支持 UDP TPROXY，未修改配置")
	}
	if enabled && boolv(args["guard6"]) && !boolv(status["ipv6_guard_supported"]) {
		return fail("设备不支持 IPv6 旁路保护，未修改配置")
	}
	targets := M{}
	if enabled {
		live, e := a.api("GET", "/proxies", nil)
		if e != nil {
			return fail("请先启动 Mihomo 核心后配置终端线路")
		}
		targets = obj(live["proxies"])
	}
	p := lanPolicy{Enabled: enabled, UDP: enabled && boolv(args["udp"]), Guard6: enabled && boolv(args["guard6"]), Clients: []devicePolicy{}}
	manifest := fmt.Sprintf("udp=%d guard6=%d\n", bint(p.UDP), bint(p.Guard6))
	listeners := named(root, "listeners")
	if listeners != nil && listeners.Kind != yaml.SequenceNode {
		return fail("现有 listeners 格式不支持")
	}
	if listeners != nil {
		keepNodes(listeners, func(n *yaml.Node) bool { return strings.HasPrefix(nstr(n, "name"), "u60-policy-") })
	}
	if enabled && listeners == nil {
		listeners = yamlSeq(root, "listeners")
	}
	seen := map[string]bool{}
	if p.Enabled && p.UDP {
		appendNode(listeners, M{"name": "u60-policy-default-udp", "type": "tproxy", "port": 17892, "listen": "0.0.0.0", "udp": true})
	}
	for i, row := range rows {
		v := obj(row)
		mac, err := macCanonical(text(v["mac"]))
		target := text(v["target"])
		if selected := text(args["selected_mac"]); selected != "" && strings.EqualFold(selected, mac) && text(args["target"]) != "" {
			target = text(args["target"])
		}
		if err != nil || seen[mac] || target == "GLOBAL" || target != "DIRECT" && len(obj(targets[target])) == 0 {
			return fail("终端 MAC 重复、无效或线路已不可用")
		}
		seen[mac] = true
		p.Clients = append(p.Clients, devicePolicy{MAC: mac, Target: target})
		port := 0
		if p.Enabled && target != "DIRECT" {
			port = 17920 + i*2
			appendNode(listeners, M{"name": fmt.Sprintf("u60-policy-%d-tcp", i), "type": "redir", "port": port, "listen": "0.0.0.0", "proxy": target})
			if p.UDP {
				appendNode(listeners, M{"name": fmt.Sprintf("u60-policy-%d-udp", i), "type": "tproxy", "port": port + 1, "listen": "0.0.0.0", "udp": true, "proxy": target})
			}
		}
		manifest += fmt.Sprintf("%s %d\n", mac, port)
	}
	if !p.Enabled {
		manifest = "udp=0 guard6=0\n"
	}
	nset(root, "u60-network-policy", p)
	keys := []string{"u60-network-policy"}
	if listeners != nil {
		keys = append(keys, "listeners")
	}
	next, e := networkBlocks(old, root, keys)
	if e != nil {
		return fail(e.Error())
	}
	var previousManifest []byte
	path := a.panelPath("network-control/manifest")
	previousManifest, _ = os.ReadFile(path)
	// Enabling changes live listeners and forwarding rules, so hold forwarded
	// traffic while the existing owner performs its atomic apply. Disabling is
	// a cleanup transaction: it must remain usable even when the optional
	// iptables/TPROXY capability is absent and must not create a new hold chain.
	if enabled {
		if _, e = a.networkScript("pause", ""); e != nil {
			return fail(e.Error())
		}
	}
	applied := false
	e = a.apply(old, next, func() error {
		command := "off"
		if enabled {
			command = "apply"
		}
		_, err := a.networkScript(command, manifest)
		applied = err == nil
		return err
	})
	if e != nil {
		// apply restores the previous YAML/listeners before we restore old hooks.
		if len(previousManifest) > 0 {
			_, e2 := a.networkScript("apply", string(previousManifest))
			if e2 != nil {
				return fail("配置已回退，终端规则恢复未确认；请切回直连")
			}
		} else {
			if _, e2 := a.networkScript("off", ""); e2 != nil {
				return fail("终端规则恢复未确认；请切回直连")
			}
			if applied {
				os.Remove(path)
			}
		}
		return fail(e.Error())
	}
	return success("终端策略已保存并回读；已有连接需重连，真实出口请按设备验证")
}
func bint(v bool) int {
	if v {
		return 1
	}
	return 0
}
