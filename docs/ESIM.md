# eSIM：可插拔 eUICC 候选能力

当前主线提供国行 B31（`BD_CNMU5250V1.0.0B31`）的可插拔 eUICC 管理，范围包括 9eSIM、5ber、eSTK.me 等，优先用用户现有 eSTK.me 实机验收。公开最新 `v0.1.16-experimental` 不含该功能；下一预览包名为 `v0.2.0-companion-preview`。当前尚未安装或完成真实卡片验收。

## 使用

SIM 卡槽插入 9eSIM、5ber、eSTK.me 等可插拔 eUICC，并使用运营商提供的有效激活码。实现通过标准 ISD-R/eUICC 接口识别和操作卡片，没有品牌白名单；这不代表各品牌所有卡片版本均已实测，卡片自身的访问限制仍会生效。普通 SIM 只能读取类型，不能下载配置；ZTE 内置 eSIM 不在支持范围。B28 的其他增强功能保留，eSIM 作业拒绝该固件。

网页进入「增强功能 → eSIM」，先点击「读取卡片」。概况、配置列表、按钮、表单和确认弹窗沿用现有增强页面的蓝白样式；展开「卡片详情」查看完整 EID、SGP.22 版本、eUICC 固件、剩余空间、默认 SM-DP+、原厂 ICCID 与读取时间等卡片提供的信息。页面显示配置名称、ICCID 首尾、启用状态与最近作业结果；展开「配置详情」查看完整 ICCID、原始名称、类别与应用 AID。小屏「管理 → eSIM 配置文件」沿用现有管理页与主题，支持读取卡片和选择已停用配置进行启用。

网页提供：

- 激活码、二维码图片、SM-DP+ 地址与 Matching ID 三种下载输入，激活码输入框也支持粘贴二维码截图；可填写确认码及可选 IMEI，点击「使用本机 IMEI」才读取设备 IMEI。二维码只在浏览器本地解析。下载完成后保持停用，不自动切换。
- 启用、停用、改名；删除要求已停用，并输入当前配置名称确认。删除不能通过程序回退恢复，重新下载可能需要新激活码。
- 运营商通知的单条或批量发送与移除，可展开查看地址与完整 ICCID。下载和删除自动尝试发送新通知；发送失败保留通知，配置操作与通知结果分别显示。

切换会短暂中断蜂窝连接。通过 USB ADB 保留管理与恢复路径，避免只通过蜂窝远程操作。如果卡片已切换而原厂固件仍报告旧 ICCID，页面提示人工重新插卡或重启；程序不会自动重启、操作分区或重启原厂守护进程。

eSTK.me 连续切换可能报告 `catBusy`。此时先等待并重新读取；持续繁忙时人工重新插卡或重启，再核对卡上状态。失败、中断或超时均不自动重试下载。激活码可能已经被服务器使用，也可能已经写入卡片，先读取核对再决定下一步。

## 实现边界

eUICC 协议桥适配自 [U60Pro-OpenUI 的固定提交](https://github.com/acloudpeng/U60Pro-OpenUI/tree/7bb9bb62e046f2c8ff272d0069cf58b2cb912564)，使用相同的 lpac 和补丁；接入本项目的登录、控制器、页面与交付链。上游的核心读卡、下载、配置管理、通知与基带重新读卡流程均保留，没有把卡片范围缩减为 eSTK.me。

与该上游版本的明确差异：本项目 eSIM 只开放准确匹配的 B31 固件；不提供跳过 SM-DP+ TLS 证书验证的选项；固件尚未识别新配置时只提示手动重插卡或重启，网页和小屏都不增加程序重启入口。小屏只读卡与切换、完整管理在网页的分工与上游一致。

卡片是 EID、配置文件、启用状态与通知的唯一 owner。`u60-esim` 通过 lpac 与 QMI UIM/QRTR 操作卡片，通过 QMI DMS/SIM 电源流程让基带重新读取；原厂 `get_sim_info` 用于核对 ICCID。网页与小屏只投影工作进程的读卡结果，不自行保存另一份配置文件状态。

现有 `panel-web` 验证原厂会话、动作白名单，再交由 `panel-control` 通过标准输入提交。协调脚本保持单一 eSIM 作业，工作进程持有现有 `/tmp/u60-control.lock`，避免同时更改蜂窝、待机或电源设置。每次修改前重新读卡并比对当前请求绑定的 EID；删除还核对卡上最新名称。升级器取得同一作业锁后才允许替换程序。

只增加 `/data/u60-panel/u60-esim`、`lpac`、协调脚本和网页资源；没有新增常驻系统服务、固件或 USB 组合改动。`/tmp/u60-esim` 为 root-only 0700，作业进度和标识只在易失读卡缓存中；重启后须重新读卡。中断结果标为未知。固件待更新提示跨作业保留，直到重新读到目标 ICCID。

SM-DP+ 只通过 HTTPS，保留 GSMA CI 与设备根证书校验。自动路径使用控制器从实际 Mihomo 配置确认的本机 HTTP 代理；也可明确选择直连或代理。程序不以关闭证书校验解决网络错误。激活码、确认码、APDU 和 HTTP 正文不进入日志或公共资料；lpac 的本地进程参数属于设备 root 管理面，不把它当作可公开诊断输出。

## 构建与验证

除现有工具链外，源码构建需要 Rust 1.87+ 和 `aarch64-unknown-linux-musl` target。仍要求 Zig 0.14.1。

```sh
rustup target add aarch64-unknown-linux-musl
ZIG=/path/to/zig-0.14.1 sh scripts/build.sh
sh scripts/test.sh
python3 scripts/package.py
```

`lpac` v2.3.0 加 9 个固定补丁，ARM64 输出 SHA-256 固定为 `66641fd6ec38a96ea4a23dc28f22c4de9b75e4252a06e35ae8a233efb4792c98`。候选包包含源码归档、补丁、构建脚本和许可；来源见 [第三方声明](../THIRD_PARTY_NOTICES.md)。准备和升级仍使用 [现有设备绑定流程](INSTALL.md)。

可在一次性 ARM64 Linux 容器验证完整控制链，不接触真实设备：

```sh
CARGO_TARGET_AARCH64_UNKNOWN_LINUX_MUSL_LINKER="$PWD/scripts/rust-linker.py" \
  ZIG=/path/to/zig-0.14.1 cargo build --locked \
  --manifest-path esim-agent/Cargo.toml --target aarch64-unknown-linux-musl

docker run --rm --platform linux/arm64 --network none -v "$PWD:/work:ro" \
  python:3.12-slim python /work/tests/esim-integration.py

docker run --rm --platform linux/arm64 --network none -v "$PWD:/work:ro" \
  python:3.12-slim python /work/tests/lpac-stdio.py /work/build/lpac/lpac
```

第一项使用真实 C/Rust 控制器、QMI 编解码与 APDU 桥，模拟基带、卡片与 lpac；第二项运行真实 ARM64 lpac。两者均不证明真实 QRTR、9eSIM、5ber、eSTK.me 或运营商服务器兼容。

网页已通过实际 Chromium 验证：SVG、PNG 与透明 PNG 的本地二维码解析，激活码/确认码/Matching ID 在确认摘要中隐藏，取消不发送请求，手填下载参数与 EID 绑定，以及 390 像素宽度无横向溢出。测试使用模拟卡片状态与合成激活码，没有连接运营商服务器或写入真实卡片。

实机验收仍需要：B31 只读识别卡片/EID/配置，优先用用户现有 eSTK.me；两配置切换后卡片、基带、原厂 ICCID 一致；蜂窝与热点恢复；一个有效激活码下载且配置可回读、可启用；通知处理；退出增强界面与 ADB 恢复。其他品牌和版本分别保留兼容性待验收状态；没有实际卡片和有效激活码时，这些项保持待验收。
