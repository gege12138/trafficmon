# trafficmon IPC 协议 v1

Linux/OpenWrt 使用权限 `0660` 的 Unix Domain Socket；Windows 开发构建使用系统 AF_UNIX。
所有整数均为小端。连接可包含多帧，实现处理 partial read/write 和 EINTR。

## 20 字节帧头

| 偏移 | 大小 | 字段 |
|---:|---:|---|
| 0 | 4 | magic：ASCII `TMIP` |
| 4 | 2 | protocol version：1 |
| 6 | 2 | command；响应设置 `0x8000` 位 |
| 8 | 4 | 请求必须为 0；响应为 status code |
| 12 | 4 | payload length |
| 16 | 4 | request id |

请求 payload 最大 64 KiB，响应最大 1 MiB。

QUERY 的 CLI 默认 limit 为 1000；wire 中 limit 为 0 时规范化为 10000，任何大于 10000
的值均被拒绝。每行固定 104 字节，因此最大合法响应为 `16 + 10000 * 104 = 1040016`
字节，不会触及 1 MiB 响应上限。编码失败会返回错误，绝不静默截断。

| 命令 | 值 | 请求 | 响应 |
|---|---:|---|---|
| STATUS | 1 | 空 | JSON 状态 |
| LIST_PERIODS | 2 | 空 | JSON 周期列表 |
| QUERY | 3 | 20 字节查询参数 | 固定编码查询行 |
| COMMIT | 4 | 空 | 空 |
| PING | 5 | 空 | `pong\n` |
| DEVICE_JSON | 6 | 同 QUERY 的 20 字节参数，必须包含 DEVICE 分组位 | JSON 设备列表 |
| DEVICE_SHOW | 7 | 同上 | 人类可读设备表 |
| DEVICE_DETAIL_JSON | 8 | DEVICE 查询参数 + NUL 结尾的规范设备 ID | 精确设备与协议明细 JSON |

QUERY 的 period 为 0（current）或规范化 `YYYYMMDD`，其余字段直接映射 Phase 1 query
spec。响应行包含 family、IP、MAC、protocol id/name 和五个 `uint64_t` 计数器，不暴露内存
struct。测试命令只编入测试 executable。

Phase 3C 在现有 20 字节查询参数的 group 位中增加 `DEVICE = 1 << 4`。DEVICE 可以与
PROTOCOL 组合，不得与 FAMILY、IP、MAC 组合。旧 QUERY 响应的行布局不变；新设备命令使用
兼容的 v1 扩展命令，不更改原有字段。DEVICE_JSON 返回 `period` 和 `devices`；每设备包含
`device_id`、`mac`、`hostname`、`addresses`、`addresses_truncated`、`protocol_id` 和
精确十进制整数计数器。`addresses` 每设备最多 32 个，超出时设置截断标志。已知 MAC 的
`device_id` 是小写 MAC；未知 MAC 是 `ip4:<地址>` 或 `ip6:<地址>`。

JSON 输出采用 ASCII-safe 策略：控制字符、引号、反斜杠及非 ASCII 协议名字节均转义，保证
输出本身是有效 UTF-8/JSON；计数器始终输出十进制整数。

Phase 4 的 DEVICE_JSON 允许在 20 字节参数后附加最多 64 个可打印 ASCII 搜索字节和 NUL；
搜索在服务端的设备分组之后、排序分页之前进行。DEVICE_DETAIL_JSON 使用同一尾部格式，
但必须是规范设备 ID 的精确匹配；格式错误返回 BAD_REQUEST。设备 JSON 新增 `total_count`
（过滤后、分页前）和 `protocol` 名称。旧 DEVICE_JSON 的 20 字节请求仍有效。

未知版本返回 `UNSUPPORTED_VERSION`。兼容的新命令可继续使用 v1；改变既有字段布局或含义必须
增加协议版本。
