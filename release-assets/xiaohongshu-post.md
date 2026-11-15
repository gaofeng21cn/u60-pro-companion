# 小红书发布文案

## 标题

给中兴 U60 Pro 换一套更好用的小屏和网络控制｜U60 Pro Companion

## 正文

给 U60 Pro 做了一套增强界面，原厂小屏、网页、热点和 ADB 都保留，双击电源键就能切换。

项目最早从开源 U60 Pro 增强项目 fork，后来陆续补上了这些功能：

- 优化中国移动终端联网的验证流程，换设备后不用反复折腾认证
- 把 Clash / Mihomo 的订阅、节点和分流规则集中到设备里管理
- 支持 Tailscale 组网、子网访问和出口切换
- 在网页端管理 eSIM 卡片、下载配置、切换和停用
- macOS 用 USB 数据线直接联网，同时保留 ADB
- Wi-Fi 接力，保存多个上游网络，自动重连和回退蜂窝
- 小屏做了深色、浅色两套主题，深色取自原厂小屏，浅色取自原厂网页，边距和操作节奏也向原厂靠齐

安装很简单：U60 Pro 用 USB 连电脑，打开 root ADB，然后对 Agent 说：

「在 GitHub 搜索 U60 Pro Companion，按 README 安装并配置到当前连接的设备。」

配图是当前版本的小屏和网页界面：总览、设置、主题选择，以及 eSIM 管理。

## 配图顺序

1. `xiaohongshu/home-settings.png`：封面，左侧总览、右侧设置。
2. `xiaohongshu/overview.png`：小屏总览，突出信号、速率、流量、代理、组网和 USB 共享。
3. `xiaohongshu/settings.png`：设置入口，突出套餐流量账本与 eSIM 配置文件。
4. `xiaohongshu/themes.png`：主题、亮度、自动熄屏和原厂节能设置。
5. `xiaohongshu/esim-web.png`：网页端 eSIM 配置文件管理。

## 话题

#中兴U60Pro #随身路由器 #TravelRouter #Clash #Mihomo #Tailscale #eSIM #macOS #开源项目 #网络工具
