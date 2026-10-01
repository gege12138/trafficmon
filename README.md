# trafficmon

`trafficmon` 是面向 OpenWrt/Linux 的轻量流量统计器。当前实现完成 Phase 0–2、Phase 2.5
和 Phase 3A，并加入 Phase 3B OpenWrt 25.12 / ImmortalWrt 24.10 的 SDK package、UCI、
procd 与逻辑网络解析，以及 Phase 3C 的设备身份元数据：
纯核心、崩溃安全的周期数据库、本地 IPC daemon、`trafficctl`，以及 Linux
conntrack/CTRZERO collector。

## 架构

```text
FlowDelta -> bounded aggregator -> versioned period database
                                      |
trafficctl <-> framed local IPC <-> daemon -> current/history query
```

- `database` 是独立、可模糊测试的内存编解码器。
- `storage` 负责临时文件、flush/fsync 和原子替换。
- `period_store` 复用 Phase 1 period API 管理当前数据库、归档和 generations。
- `ipc_protocol` 与套接字 I/O 分离；CLI 不直接读取数据库。

格式文档见 `docs/database-format.md` 和 `docs/ipc-protocol.md`。
MAC/主机名来源、缓存边界和设备查询行为见 `docs/identity.md`。

协议文件使用显式稳定 ID：`stable-id name protocol port`，例如 `20 HTTPS tcp 443`。
数据库 V2 持久化规范化协议配置的指纹；指纹不一致时拒绝加载，而不是重解释历史 ID。

## 构建和测试

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Clang 或非 Windows GCC 可使用 `-DTRAFFICMON_ENABLE_SANITIZERS=ON`。Windows + LLVM-MinGW
会把 ASan 所需 DLL 自动复制到构建目录，不会提交到仓库。

## daemon 和 CLI

```sh
trafficmon --database-dir /var/lib/trafficmon \
  --socket /run/trafficmon/trafficmon.sock --period-rule 1 \
  --limit 10000 --generations 10 --protocol-db /usr/share/trafficmon/protocols \
  --commit-seconds 86400

trafficctl status
trafficctl list
trafficctl show --group ip --order total --desc
trafficctl json --period current
trafficctl json --group device
trafficctl show --group device
trafficctl csv --period 2026-09-01
trafficctl commit
```

Linux collector 通过一个或多个 `--local-subnet CIDR` 启用；另可设置
`--refresh-seconds` 和 `--netlink-buffer`。权限、CTRZERO/DESTROY 竞态和 ENOBUFS 行为见
`docs/collector.md`。

退出码：`0` 成功、`1` 一般错误、`2` 用法错误、`3` daemon 不可用、`4` IPC 错误、
`5` 数据库/查询错误。`SIGINT`/`SIGTERM` 会请求提交并干净停止。

QUERY 默认返回上限为 1000 条，协议硬上限为 10000 条；超过硬上限的请求会被拒绝。
最大合法二进制响应为 1,040,016 字节，小于 1 MiB IPC 上限，编码器不会截断响应。

数据库目录包含 `current.db` 和内部生成的 `period-YYYYMMDD.db`。IPC 只接受规范化日期，
不能把查询字符串直接映射为路径。

## 已知限制

- 共享的 OpenWrt/ImmortalWrt daemon package、UCI 与 procd 位于 `openwrt/`，ImmortalWrt 的
  构建与 opkg 验证入口位于 `immortalwrt/`。独立 LuCI package 位于 `luci-app-trafficmon/`，
  页面、权限与安全边界见 `docs/phase4-luci.md`；headless daemon 不依赖 LuCI。
- Phase 2 使用未压缩数据库；gzip 延后。
- daemon 当前为串行本地 IPC 服务。
- 生产二进制没有计数注入命令；确定性注入只存在于单独测试 executable。

行为基线参考 nlbwmon，但未复制其源码结构。
