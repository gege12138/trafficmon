# Phase 4 LuCI 前端

## 包与安装

`trafficmon` 仍为无 LuCI 依赖的 daemon/CLI 包；`luci-app-trafficmon` 是独立包，依赖
`luci-base` 和 `trafficmon`。同一 `luci-app-trafficmon/Makefile` 由 ImmortalWrt 24.10.4
SDK 生成 IPK、由 OpenWrt 25.12.x SDK 生成 APK。两个 SDK 都按其包基础设施执行：

```sh
sh openwrt/prepare-sdk-package.sh /path/to/sdk
sh luci-app-trafficmon/prepare-sdk-package.sh /path/to/sdk
make -C /path/to/sdk package/trafficmon/compile package/luci-app-trafficmon/compile NO_DEPS=1
```

安装后在「Status → Traffic Monitor」查看。页面为 Overview、Devices、Device Detail、
History、Settings / Status。删除 LuCI 包不应删除 `/etc/config/trafficmon`、
`/etc/trafficmon/protocols` 或流量数据库；这些不属于 LuCI 包。

## 架构与权限

浏览器只通过 LuCI 已认证的 `file.exec` 调用 `/usr/libexec/trafficmon-luci`。
该只读 helper 校验命令、周期、排序、limit 和 offset，把参数作为独立 argv 传给
`trafficctl`，不拼接或执行 shell 命令字符串。它不提供 `commit` 或服务控制。
`trafficctl` 通过现有受权限控制的 Unix socket 与 daemon 通信；socket 不暴露给浏览器，
daemon 不启动 HTTP 服务。ACL 的 read 范围仅有该 helper 的 `exec` 和 trafficmon UCI
读取；write 范围仅为 trafficmon UCI。没有任意文件读取、shell 或网络/防火墙权限。

设置使用标准 LuCI `form.Map` 和 UCI `set → apply`；现有 procd 配置变更触发器只重载
trafficmon。`local_network` 可填逻辑网络名或多个 CIDR；UI 只做空白/长度的基本检查，
实际 CIDR 和周期规则仍由后端启动时验证。故错误配置会使 trafficmon 拒绝启动，
不会触发网络或防火墙服务重启。

## 查询与导出

Overview 汇总两种 IP family 的后端聚合结果，并请求前 50 个设备；Devices/History 每页
50 个设备。搜索在后端分组之后、排序分页之前执行，设备详情以规范 device ID 精确过滤。
普通页面不拉取所有原始记录。JSON/CSV 导出使用 CLI 原始查询；LuCI `exec_direct`
通过有相同 ACL 的 CGI 通道传送大型导出，避免 ubus 响应限制。导出仍受后端 10,000 行
查询上限约束。若某周期原始记录超过 10,000 条，导出仅含前 10,000 条；History 页面明确提示此限制。

主机名与设备 ID 均作为 DOM 文本节点呈现，不使用 `innerHTML`。历史流量的主机名是
*当前* 身份元数据，可能不同于当时名称；历史设备分组不借用当前 IP→MAC 缓存。
设备明细的 IPv6 地址默认折叠显示，后端最多列出 32 个地址并提示截断。
没有自动秒级轮询或图表框架；Overview 仅合计后端返回的最多两行 IP family
汇总，不在浏览器中按原始流量重新计账。

## 已知限制

- 当前 IPK/APK 构建目标是 x86/64。LuCI 包无架构相关二进制，但 SDK 元数据标记为 x86_64。
- 地址列表超过 32 项时只能显示前 32 项；后端原始 IP 查询仍保留完整记录。
- JSON 设备分组提供协议名称，但历史名称无法还原为当时的 DHCP 名称。
- 页面的人类可读字节单位使用浏览器数值格式；原始 JSON/CSV 导出及 IPC 计数保持后端整数。
- 未实现 LuCI 翻译目录、实时带宽图、QoS、DPI 或流量控制。

Production deployment: NOT PERFORMED — intentionally deferred for remote-router safety.
