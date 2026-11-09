# Agent 只读接入

Agent 通过可选 [U60 Connector](../integrations/opl-u60-connector/README.md) 读取同一个 `panel-control` owner。设备设置继续由小屏、原厂认证网页和既有控制器负责；通用会话视图不增加控制或凭证入口。

包位于 `integrations/opl-u60-connector`，包括现有 OPL Package descriptor、标准 contribution adapter 与本地 Skill。`u60.status` 提供设备身份摘要、固件、观测时间、版本化状态和网页交接；`u60.diagnostics` 提供原 `diag.network` 的分项结果。诊断会发出有限 DNS/HTTPS 探测，结果只证明设备本机的对应路径。

首次读取要求一个明确 ADB 目标，后续用 `expected_identity_sha256` 绑定。可复用安装准备材料中的 `TARGET-IDENTITY-SHA256`，配置一次宿主环境 `U60_TARGET_IDENTITY_SHA256`。每次调用前后核对设备，原始 IMEI、原厂会话、代理密钥及私人配置不进入返回值。设备端无新增服务、监听端口或安装步骤。

状态读取成功不证明热点客户端联网；诊断生成成功不证明全部检查通过。连接中断、控制器缺失或不兼容固件返回 `unavailable`，不伪装为离线或读取旧结果。快照按 `observed_at` 与 30 秒有效期显示，`revision` 仅标识观测，不授予设备写权限。

主机验证从 `python3 tests/test-agent-connector.py` 进入，覆盖目标切换、非支持固件、多设备、断线、原 owner 失败、脱敏、命令白名单和独立复制包调用。Package 为开发接入，设备安装、原生宿主呈现和实机诊断仍需在相应交付中分别验收；本入口不扩大 [现有设备验证范围](VALIDATION.md)。
