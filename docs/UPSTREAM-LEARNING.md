# 开源吸收对照

本项目把 GitHub 仓库当作实现与交互的学习来源，现有 UBUS、Mihomo、eSIM 作业锁和蓝白页面仍是本地 owner。每项能力先固定提交，再按许可证和本地运行合同吸收；上游自报的设备结果不转成 B31 验收结论。取舍优先级是明确收益、复用现有 owner、可独立停用；新增常驻服务、第二状态 owner 或未经实机验收的出口不因上游已有就默认纳入。

| 上游固定提交 | 学到的实现 | 本地落点 | 保留的差异 |
| --- | --- | --- | --- |
| [SadykovIV/ZTE_U60Pro e300d669](https://github.com/SadykovIV/ZTE_U60Pro/tree/e300d66924a78925ac901bb6826b3ba4e67ce52f) | 电脑使用自己的 HTTPS，设备只负责 eUICC/APDU/RPC；下载完成后单独处理通知 | `tools/esim-computer.py` 的 loopback CONNECT、`adb reverse` 和现有 `u60-esim` stdin 作业 | 没有引入 SSH 常驻服务或另一套桌面 UI；凭据只从 stdin 进入；未知结果不自动重试 |
| [acloudpeng/U60Pro-OpenUI 7bb9bb62](https://github.com/acloudpeng/U60Pro-OpenUI/tree/7bb9bb62e046f2c8ff272d0069cf58b2cb912564) | lpac、QMI/QRTR 和 eUICC 生命周期的 B31 适配 | `esim-agent/`、`panel/esim-control.sh`、`docs/ESIM.md` | 代码按 MIT 许可保留来源；AGPL lpac、LGPL libeuicc 分开随包；9eSIM/5ber/eSTK.me 等由同一 SGP.22 路径处理，不为单一卡品牌写分支 |
| [GodBlessAmerica/zte-u60pro-proxy-mode fd4f211c](https://github.com/GodBlessAmerica/zte-u60pro-proxy-mode/tree/fd4f211c55001a00dfe6b037726a99a4af28959c) | MAC 作为稳定设备身份，TCP/DNS/UDP/IPv6 开关分离 | `web/controller/network.go`、`panel/network-control.sh`、网络页 | 复用本项目 Mihomo，不复制其独立 Go daemon；IPv6 guard 只防旁路，不宣称全量 IPv6 代理 |
| [huiyi9420/xiaohaiguan 80096fe6](https://github.com/huiyi9420/xiaohaiguan/tree/80096fe6e1568488ddc0e3d37d605909f6a52bab) | Mihomo listener/TPROXY、真实 UDP 流量观测和诊断降级 | `network.go` 的 listener 写入、`network-control.sh` 自有链、`udp_observation` | 仍由原有 Go YAML 事务和单 writer 写配置；无真实连接样本显示未知 |
| [czwstc/MU5250-Wireguard 3ca0e1f3](https://github.com/czwstc/MU5250-Wireguard/tree/3ca0e1f394d8ab2a121b6cab726d83e58fe81cef) | 单 Peer `.conf` 校验、私钥不回显、保存与启用分离、失败回退 | 仅保留本文件的研究记录，当前控制器与网络页不维护导入器 | 上游使用内核 WireGuard；本项目没有 B31 真实出口验收，不增加第二个网络 owner |
| [scoltzero/remix 6d9c6679](https://github.com/scoltzero/u60pro-devui-remix/tree/6d9c6679b36c4c7baf790335b35e2f0) 与 [huangtengsz-ui 4d1baddb](https://github.com/huangtengsz-ui/u60pro-screen-ui/tree/4d1baddb7f1fb0292869e767aea45221fa7b3f2) | 按 ICCID 持久流量账本、下载优先、小屏分组与固定返回 | 现有 `panel-ledger.h`、`panel-shell-ui.h`、统一蓝白网页 | 台账是设备测量估算，不冒充运营商计费；网页高级输入不塞进屏幕 litehtml |

许可证事实：上述固定上游中的 agent/web 组件按各自 MIT 文本使用；OpenUI 的 screen、lpac 和 libeuicc 许可不同，已分别放入 `licenses/`，不能用根目录许可证覆盖。源码研究报告和快照位于 `build/u60-survey-20261014/github/`，设备私有响应不进入公开交付物。

## 最终取舍与落实状态

| 学习点 | 决定 | 2026-10-14 状态 |
| --- | --- | --- |
| eSIM 与电脑代联网 | 吸收。复用 lpac/QMI/QRTR、卡片 owner 和现有作业锁；电脑工具按需使用，不运行常驻代理服务。 | 源码、主机模拟、UI 和本地 ARM64 候选包已有；真实 B31 卡片读写、eSTK.me 切换、SM-DP+ 下载及蜂窝恢复仍待验收。 |
| 按设备 TCP/DNS | 有条件吸收。沿用 Mihomo 与现有配置 owner，作为显式可选模块。 | 已完成能力探测、默认关闭、独立启停、只清理自有 listener/规则和主机回读；B31 真实终端业务仍待验收。 |
| UDP TPROXY | 不默认启用，独立保留为可选模块。 | 已接入内核/iptables/Mihomo 探测、独立开关、回退和活跃连接观测；规则存在不等于热点 UDP 真实命中，B31 真实业务仍待验收。 |
| IPv6 旁路保护 | 与 IPv6 代理明确区分，只有独立开关、探测和回读通过后才启用。 | 已接入独立探测、启停和自有规则清理；仍未证明完整 IPv6 代理，也未完成 B31 泄漏/回退验收。 |
| WireGuard 导入 | 暂缓。只保留上游研究结论，不在当前项目维护解析器、导入 UI 或多配置删除引用逻辑。 | 当前控制器、脚本和网页入口已撤下；没有 B31 真实 VPN 出口验收。重新纳入前需有明确需求和现有 Mihomo owner 的低维护接法。 |
| UI、指标与流量账本 | 吸收信息层级、未知/过期语义和按 ICCID 台账，不替换 renderer 或现有账本。 | 统一蓝白 UI 调整在候选源码中；按卡台账复用现有实现。新网络能力仍以真实控制器状态显隐。 |

网络能力最终按“原厂/现有 Mihomo 基线 + 显式可选模块”组织。TCP/DNS、UDP、IPv6 guard 分别拥有能力状态和启停状态；模块关闭时撤销自己的配置与规则，绝不清空原厂链、改默认路由或代替 Mihomo owner。缺依赖时只隐藏/禁用该模块，其他模块与默认联网不受影响。当前候选已通过主机回归并闭环上述模块合同；由于没有 B31 真实终端业务、开机恢复和出口切换证据，网络增强仍标为候选，不写成已交付能力。
