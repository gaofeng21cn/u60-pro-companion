# 电脑网络写入可插拔 eSIM

`esim-computer.py` 用电脑的网络为 U60 Pro 卡槽中的可插拔 eUICC 执行首装。它复用设备已有的 `u60-esim`、lpac、QMI/APDU 和设备侧 curl；电脑只提供一次性 `127.0.0.1` CONNECT 隧道，经 `adb reverse` 送到设备。HTTPS 证书仍由设备侧 curl 验证，代理不会解密 TLS。

这条路径适用于实现标准 SGP.22 的可插拔 eUICC，包括 9eSIM、5ber、eSTK.me 以及其他兼容卡。项目不会按品牌写死白名单；卡片自身的访问控制、运营商激活码和设备固件支持仍然决定最终结果。

## 使用

先确认电脑只连接一台 U60 Pro，或显式指定 ADB serial：

```sh
python3 tools/esim-computer.py --serial <adb-serial> scan
python3 tools/esim-computer.py --serial <adb-serial> download
```

交互式下载的激活码和确认码使用隐藏输入，不出现在命令行参数中；可选 IMEI 直接输入或留空。工具会先读取 EID，确认后再执行。自动化调用必须把 JSON 从 stdin 传入，并同时使用 `--yes` 和 `expected_eid`：

```sh
printf '%s' '{"activation_code":"LPA:1$smdp.example$matching-id","expected_eid":"<32位EID>"}' |
  python3 tools/esim-computer.py --serial <adb-serial> --request-stdin --yes download
```

激活码、APDU 和 HTTP 正文不会写入电脑端日志。代理只接受本次 SM-DP+ 以及本次作业产生的精确通知地址，目标端口通常为 443；不接受任意 HTTP 转发、域名后缀通配、localhost、内网或链路本地地址。

下载终态由设备 worker 持有现有 `/tmp/u60-esim/job.lock` 和 `/tmp/u60-control.lock`，代理会一直保持到终态。设备断开、超时或返回不完整 JSON 时，工具只报告“写卡结果未知”，不会自动再次提交激活码；应重新执行 `scan` 后再判断。下载成功但运营商通知地址在下载中才出现时，工具会读取卡片新增通知并单独处理；通知失败会保留在卡上，可稍后用 `notify` 处理。

## 限制

设备侧当前只接受准确匹配的国行 B31 固件，且必须安装本项目的 eSIM worker。电脑端连接只解决设备无法直接访问 SM-DP+ 的网络路径，不改变卡片、运营商或固件的兼容性。工具不会自动启用新配置，也不会自动重启设备或重新提交失败下载。
