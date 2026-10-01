# Phase 3C 设备身份层

计账键仍是 IP、可选 MAC、协议；数据库格式仍为 V2。主机名仅为查询时读取的可变元数据，
既不影响 conntrack 分类，也不影响计数器。MAC 未知的流量照常计账，`mac` 为 `null`，
不生成伪造 MAC。

## MAC 来源与安全边界

Linux `NETLINK_ROUTE` 的 `RTM_GETNEIGH`/`RTM_NEWNEIGH` 返回 ARP/NDP 邻居。
仅接受 UCI 逻辑网络解析出的本地物理/桥设备（或显式 `local_interface`）的邻居，
且地址必须属于现有 `local_network` CIDR。接受 REACHABLE、STALE、DELAY、PROBE、
PERMANENT，以及有有效 6 字节单播硬件地址的 NOARP。FAILED、INCOMPLETE 和 DELNEIGH
立即使已有缓存映射失效。缺失、全零、组播或非 6 字节 MAC 不会被缓存。

每 5 秒最多请求一次邻居快照；不为每条 conntrack 记录遍历内核邻居表。缓存最多 1024 个
IP→MAC 映射，300 秒过期；相同 IP 的新 MAC 立即替换旧映射，容量满时替换最旧条目。
如果邻居稍后才出现，后续流量会带 MAC；设备查询也可把当前周期早期未知 MAC 的 IP 记录
按当时缓存归并，但不会改写历史数据库。历史周期不使用当前 IP→MAC 映射，以免 DHCP 地址
重用造成误归属。

## 主机名来源与优先级

OpenWrt/ImmortalWrt 默认读取 `/etc/config/dhcp` 中的 `config host` + `option name`，
按 MAC（或无 MAC 时按 IP）关联；也读取 dnsmasq 的 `/tmp/dhcp.leases`，跳过 `*`/过期
主机名。静态配置优先级 2，高于活动租约优先级 1；缺失的租约主机名不会擦除静态名称。
两种文件每 30 秒重新读取。主机名改变只更新显示，不建立新的计账设备。未配置 dnsmasq
或缺乏可解析主机名时显示 `null`。

主机名缓存最多 512 项、每名称最多 63 字节。租约路径可用 daemon 的 `--dhcp-leases`
指定，静态配置路径可用 `--static-hostnames` 指定。当前仅解析常见的单 MAC UCI 静态 host
和 dnsmasq IPv4 租约文本格式；不解析 odhcpd 独立的 IPv6 租约文件。IPv6 地址仍可通过
NDP MAC 与已有 IPv4 租约的设备主机名关联。

## 查询

`trafficctl json --group device` 与 `trafficctl show --group device` 使用同一查询聚合器。
MAC 已知时跨 IPv4、多 IPv6 地址合并；未知时确定性回退到 IP。可加第二个
`--group protocol`。原有 `--group ip`、`--group mac`、`--group protocol` 保持可用。
JSON `addresses` 最多列出每设备 32 个不同 IP，超过会标明 `addresses_truncated`。
原始 IP 记录保留，可用普通 `trafficctl json` 查询完整明细。

设备查询响应受 IPC 1 MiB 上限约束；过大时显式返回错误，不静默截断计数。
