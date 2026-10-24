# 小红书发布文案

## 标题

把中兴 U60 Pro 做成真正好用的随身路由器｜U60 Pro Companion

## 正文

手里的中兴 U60 Pro，终于有一套更完整的增强体验了。

U60 Pro Companion 是我从开源项目 fork 出来、再持续重构和完善的一套 U60 Pro 伴侣系统。现在已经把小屏、原厂网页和网络能力整合到一起，项目也吸收了 GitHub 上多个开源项目的实现经验。

这次重点完善了几件事：

1. 中国移动终端联网：完善原厂认证链路和设备侧状态保持，联网体验不再被单一验证设备限制。
2. Clash / Mihomo：订阅、节点、规则分流、直连和实际接管状态集中管理。
3. Tailscale 组网：登录、设备访问、子网转发和出口状态都能在设备上管理。
4. eSIM：支持读取卡片、查看 EID 和配置文件、下载、启用、停用、改名、删除和运营商通知。
5. macOS USB 直连：U60 Pro 通过原厂 ECM＋ADB 直接给 Mac 提供网络，数据线同时保留 ADB。
6. Wi-Fi 接力：保存上游 Wi-Fi、自动重连和蜂窝回退，出门时可以把 U60 Pro 当作 travel router 使用。
7. 小屏 UI：深色和浅色两个主题，深色取自原厂小屏，浅色取自原厂网页；信息层级、边距、卡片和交互都重新整理，操作节奏尽量贴近原厂。

安装也很简单：

把 U60 Pro 用 USB 连接到电脑，启用 root ADB，然后让你使用的 Agent 在 GitHub 搜索 `gaofeng21cn/u60-pro-companion`，阅读 README 后按设备生成绑定材料并执行安装。也可以直接下载 Release 安装包，按文档完成准备、校验、安装和启动。

项目地址：
https://github.com/gaofeng21cn/u60-pro-companion

配图是当前版本的小屏和网页界面预览：总览、设置、主题选择，以及 eSIM 管理页面。后续还会继续把设备状态、界面细节和网络体验打磨得更顺手。

## 配图顺序

1. `xiaohongshu/home-settings.png`：封面，左侧总览、右侧设置。
2. `xiaohongshu/overview.png`：小屏总览，突出信号、速率、流量、代理、组网和 USB 共享。
3. `xiaohongshu/settings.png`：设置入口，突出套餐流量账本与 eSIM 配置文件。
4. `xiaohongshu/themes.png`：主题、亮度、自动熄屏和原厂节能设置。
5. `xiaohongshu/esim-web.png`：网页端 eSIM 配置文件管理。

## 话题

#中兴U60Pro #随身路由器 #TravelRouter #Clash #Mihomo #Tailscale #eSIM #macOS #开源项目 #网络工具
