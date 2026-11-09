---
name: opl-u60-connector
description: Read a locally connected U60 Pro Companion device status or run its bounded network diagnostics when the user asks about U60 health.
---

# U60 只读状态

优先使用已发现的 `u60.status` 或 `u60.diagnostics` 共享视图。无图形宿主时，从本 Skill 目录运行 `printf '{}' | python3 ../../connector.py status`。

保留 status 返回的 `device.identity_sha256`，后续请求用 `expected_identity_sha256` 绑定同一设备。`diagnostics` 必须有此绑定，调用原设备 `diag.network`；检查 `checks` 的每项结果，不能把 `state=ready` 当作网络全通过。30 秒后刷新，不使用旧快照推断在线状态。

连接模块仅允许只读调用。需要设置时交接 status 的 `handoff.url`，仍由原厂登录和专业网页管理。不要读取密码、eSIM 身份或执行任意 ADB 命令；安装、恢复、切换 USB 和网络操作沿用设备专用流程及用户授权。
