# U60 Pro Companion

为中兴 U60 Pro（MU5250）国行 B28 / B31 提供与原厂共存的小屏、网页和网络控制能力。小屏负责状态查看与常用操作，增强网页负责输入和高级管理；原厂固件、热点、管理页、ADB 与双击电源切换入口继续保留。

这个仓库已经是独立维护的产品主线，不是上游项目的原样分支。公开最新 Release 仍是 [`v0.1.16-experimental`](https://github.com/gaofeng21cn/u60-pro-companion/releases/tag/v0.1.16-experimental)；当前 `main` 是下一预览周期，默认构建版本为 `v0.2.0-companion-preview`，尚未作为正式公开 Release 发布。主线包含后续的小屏主题、设备绑定交付、周期流量台账、SIM 卡槽识别和部署维护改进。

当前事实、安装合同和验收边界分别由[产品基线](docs/BASELINE.md)、[安装说明](docs/INSTALL.md)、[验证范围](docs/VALIDATION.md)和[文档导航](docs/README.md)维护。源码、设备安装、真实业务和公开包是不同的验收层级。

## 当前能力

| 能力 | 当前状态 |
|---|---|
| 原厂共存小屏 | 已接入原厂小屏切换；提供“深色”和“浅色”主题，按原厂小屏的间距、黑底和状态色整理；主题与双击切换持久保留 |
| 增强网页 | 复用原厂登录与管理通道，提供总览、网络、代理、组网、电源和工具入口 |
| Clash / Mihomo | 订阅、节点偏好、规则/全局/直连、实际接管诊断；当前合同覆盖 IPv4 TCP 与 DNS，普通 UDP 和 IPv6 全量代理不承诺 |
| Tailscale | 登录、设备访问、子网转发和出口相关状态按实际回读呈现；身份保存在设备本机 |
| Wi-Fi 接力 | 2.4 GHz 与非 DFS 5 GHz 上游、保存网络、优先级、失败重试和蜂窝回退策略 |
| USB 网络 | LAN / AUTO 角色协调与已识别驱动入口；具体网卡、供电和多网卡组合需分别验收 |
| B31 Mac USB 联网 | 通过中兴原厂 owner 提供 ECM＋ADB，Mac 免驱；启停、ADB 共存、拔插和冷启动恢复已有一台设备证据 |
| 套餐流量台账 | 按当前活动 SIM 卡槽的蜂窝累计字节估算；支持固定周期、首周期锚点、额度和有效期，不伪造采样前历史用量 |
| eSIM | 源码和交付链候选能力，面向 B31 可插拔 eUICC；真实读卡、切换和运营商下载仍待设备验收 |

明确未承诺的范围包括普通 UDP、IPv6 全量代理、DFS/6 GHz、企业 Wi-Fi、认证门户自动处理、所有 USB 网卡和扩展坞组合、Windows 全流程、长时功耗与所有设备批次。详见[验证范围](docs/VALIDATION.md)。

## 支持的设备

| 固件 | 要求 |
|---|---|
| 国行 B28 | `BD_FLYMODEMMU5250V1.0.0B28`，内核 `5.15.185-perf` |
| 国行 B31 | `BD_CNMU5250V1.0.0B31`，内核 `5.15.194-perf` |

不支持 B27、港版/国际版及其他固件。项目不提供 ADB 解锁、刷机固件或救砖镜像；不要修改 USB 组合或安装不匹配的内核模块。

## 安装与升级

电脑需要 Python 3、Google Android Platform Tools（`adb`）和可访问 GitHub 的网络。准备与安装时只连接一台目标设备，启用 root ADB，并拔下外接 USB 网卡；设备 `/data` 至少保留 400 MB 可用空间，升级可能需要更多。

从公开 Release 或自己构建的主线预览包安装时，下载对应的 `.tar.gz` 和 `SHA256SUMS.txt`，不要使用 GitHub 自动生成的 Source code 压缩包。公开 Release 目前仍是 `v0.1.16-experimental`，下面以当前主线默认预览包为例。设备绑定材料只适用于准备它的那台设备，不要分享或复用：

```sh
shasum -a 256 -c SHA256SUMS.txt
tar -xzf u60-pro-enhanced-v0.2.0-companion-preview.tar.gz
cd u60-pro-enhanced-v0.2.0-companion-preview
adb devices
python3 prepare.py
cd ../u60-prepared-private
python3 deploy-from-computer.py check
python3 deploy-from-computer.py install
python3 deploy-from-computer.py start
```

已有安装使用原地升级，不执行首装覆盖：

```sh
python3 deploy-from-computer.py upgrade-check
python3 deploy-from-computer.py upgrade
```

升级器会检查固件、设备身份、回退材料、USB 状态和可用空间，逐文件备份并核对替换结果。上传新材料前会清理 `/data/u60-packages/` 中已验证的旧上传包；回退备份、用户配置、订阅、节点偏好和 Tailscale 身份独立保留。准备、升级失败或设备异常时，按[恢复说明](docs/RECOVERY.md)处理。

安装后先确认小屏、原厂网页、热点和 ADB，再按需启用代理、组网、Wi-Fi 接力或 USB 网络。B31 默认不主动开启这些可选协调服务。

## 日常入口

- 小屏：总览查看出口、速率、蜂窝用量和连接设备；代理、组网、网络和设置页面提供常用操作。单击电源亮灭屏，双击在原厂与增强界面之间切换。
- 网页：连接 U60 热点或手动 LAN，使用原厂管理密码登录，再进入“增强功能”。代理对应 Clash / Mihomo，组网对应 Tailscale；没有额外默认密码。
- 套餐台账：在“设置 → 套餐流量账本”设置额度、固定周期和有效期。统计是设备本地估算，不替代运营商账单。

详细操作见[使用说明](docs/USAGE.md)、[Mac USB 直连](docs/MACOS-RNDIS.md)、[Wi-Fi 接力](docs/WIFI-RELAY.md)、[网络诊断](docs/NETWORK-DIAGNOSTICS.md)和[eSIM 说明](docs/ESIM.md)。不要把管理端口映射到公网，也不要在 Issue 中发送密码、订阅、设备标识或私有准备目录。

## 从源码构建

开发构建需要 Zig 0.14.1、Go、Python 3、Rust 1.87+（含 `aarch64-unknown-linux-musl` target）、C 编译器和 Node.js：

```sh
ZIG=/path/to/zig-0.14.1 sh scripts/build.sh
sh scripts/test.sh
python3 scripts/package.py
```

默认构建目录是 `dist/u60-pro-enhanced-v0.2.0-companion-preview/`；`u60-pro-enhanced-*` 是设备升级兼容标识，暂不改动。主机测试和 ARM64 构建不能替代真实设备、下游业务、长待机或恢复验收。

## 来源与许可

项目早期的小屏、网页增强和部分网络能力参考并吸收了 [defilippisprafka-netizen/u60-pro-enhanced](https://github.com/defilippisprafka-netizen/u60-pro-enhanced) 及其他开源项目的实现；当前控制器、设备绑定交付、原厂共存 UI、B31 ECM＋ADB、周期台账和验收边界由本仓库独立维护。具体来源、许可证和本地取舍见[开源吸收对照](docs/UPSTREAM-LEARNING.md)、[LICENSE](LICENSE)和[第三方声明](THIRD_PARTY_NOTICES.md)。
