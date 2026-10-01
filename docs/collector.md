# Linux conntrack collector

collector 使用原始 `NETLINK_NETFILTER`/`NFNL_SUBSYS_CTNETLINK`，不依赖
`libnetfilter_conntrack`。Netlink 传输、二进制解析、Phase 1 分类和聚合保持分层。

```sh
trafficmon --local-subnet 192.168.1.0/24 --local-subnet fd00::/64 \
  --refresh-seconds 30 --netlink-buffer 524288
```

没有 `--local-subnet` 时 collector 禁用。需要 root 或等价 `CAP_NET_ADMIN` 权限，以及内核
`nf_conntrack_netlink`。流量计数还要求 `net.netfilter.nf_conntrack_acct=1`；未启用时内核
可能只返回零计数。

启动时执行一次 `IPCTNL_MSG_CT_GET_CTRZERO` dump。已有连接的当前计数会被纳入，但不会被
计为新连接。NEW 事件只增加一次连接数；DESTROY 事件加入自最后一次 CTRZERO 复位后的剩余
计数。

CTRZERO 对单个 conntrack entry 的读取与清零由内核原子完成。因此 dump 返回清零前计数，
随后 DESTROY 返回清零后的剩余计数；两者都应累加。实现不会忽略 DESTROY，也不会把周期
dump 当成新连接。namespace 测试覆盖多次 dump 后销毁的长连接及 dump 边界批量短连接。

`ENOBUFS` 会把本次进程状态标为 `degraded`、递增诊断计数并安排新的 CTRZERO dump。后续
dump 不能恢复已丢失数据，因此状态不会谎报为完全可靠。有效接收缓冲区受
`net.core.rmem_max` 限制；低于请求值时仅记录一次警告。

特权测试默认跳过：

```sh
sudo env TRAFFICMON_RUN_PRIVILEGED=1 \
  ctest --test-dir build -L conntrack --output-on-failure
```
