# U60 只读连接模块

此可选开发包通过电脑已授权的 USB ADB 调用已安装的 `panel-control`，共享 U60 小屏和原厂网页的状态 owner。它不安装程序、不保存凭证、不暴露任意 shell 或设备控制动作。源码包仍为 `development_only`，尚未声明独立发布或实机安装验收。

需要 Python 3、Google ADB 与已安装 U60 Pro Companion 的国行 B28/B31。多设备时在可信宿主环境设置 `U60_ADB_SERIAL`；自定义 ADB 路径使用 `U60_ADB`。这些路由选项不接受网页输入。

```sh
printf '{}' | python3 connector.py describe
printf '{}' | python3 connector.py status
printf '{"expected_identity_sha256":"<status 返回的 64 位 identity_sha256>"}' | python3 connector.py diagnostics
```

可将已有准备目录 `TARGET-IDENTITY-SHA256` 的摘要配置为宿主环境 `U60_TARGET_IDENTITY_SHA256`，避免反复输入。摘要算法与现有安装器相同；原始 IMEI 不通过 ADB stdout 返回。`status` 首次无绑定时要求恰好一台设备；后续调用携带摘要或使用环境绑定，拒绝目标变化。`diagnostics` 必须有绑定，调用 `diag.network` 的有限 DNS/HTTPS 测量，不更改网络配置。

`observed_at` 使用电脑 UTC 时间；结果是一次读取，30 秒后应刷新。`revision` 是此次脱敏观测的摘要，不是写操作授权。每次读取都重新核对固件和设备摘要，并在读取结束后复核身份。断线、目标变化、非支持固件或 owner 错误返回结构化 `unavailable`，不重试、不返回旧快照。诊断调用成功与网络检查通过分开记录。

共享 `u60.status`/`u60.diagnostics` 视图使用标准 Package contribution adapter。状态中 `handoff.url` 来自同一 owner 返回的 RFC1918 管理地址，浏览器仍需原厂登录；先核对显示的设备摘要再操作。凭证、eSIM 信息、Wi-Fi 密码、节点订阅与完整配置均不进入此投影。没有图形宿主时，上述 CLI 保留同样的结构化结果。

主机测试：在仓库根运行 `python3 tests/test-agent-connector.py`，使用合成 ADB 和临时复制的完整包验证；不会连接或写入设备。
