# 自动候选与正式发布

`product-version.json` 维护产品版本。OPL 统一每日产品任务根据主线交付变化准备
版本，经 PR/CI 合入后创建 annotated `vX.Y.Z-companion` tag。纯文档变化跳过。

`ci.yml` 运行主机回归；`release.yml` 使用固定 Zig 与 Rust 工具链构建 ARM64
程序，并通过原有打包器生成归档。归档压缩头与成员时间固定，保留原文件权限和
排序。候选 Actions artifact 同时保存源码 commit/tree、归档 SHA-256 和主机验收
范围，不生成第二套 GitHub Release，也不更新真实设备。

候选通过之后，由实际设备验收产生 `u60.device-release-qualification.v1` 回执。
回执只记录源码 commit/tree、归档 SHA-256、B28/B31 固件类型、验收项目和
`approved` 判断，不包含设备编号、地址、配置或日志。必需项目是安装字节一致、
程序启动、私有状态保留、网络保留与升级恢复。主机测试不代替这些判断。

在 GitHub 运行 `Publish Device Qualified U60 Release`，输入成功候选的 run ID 与
对应回执 JSON。工作流先核对原始 run、主线源码、候选和实机回执，再通过既有
tag 公开同一原始归档；已发布资产只能核对复用，不覆盖。发布说明标明实际验收
的固件范围，公开资产按 GitHub digest 回读。

缺少实机回执时保留候选与当前稳定版。后续 Codex 可以管理这两个工作流和发布
状态，实际设备安装仍须绑定明确目标及其授权。
