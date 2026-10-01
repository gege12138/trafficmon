# trafficmon 数据库格式 v2

多字节整数均为无符号小端编码。文件必须精确等于 `64 + record_count * 72` 字节；禁止
直接序列化含 ABI padding 的 C struct。

## 64 字节文件头

| 偏移 | 大小 | 字段 |
|---:|---:|---|
| 0 | 8 | magic：ASCII `TRAFDB02` |
| 8 | 2 | format version：2 |
| 10 | 2 | header size：64 |
| 12 | 2 | record size：72 |
| 14 | 2 | 保留，必须为 0 |
| 16 | 4 | record count |
| 20 | 4 | period start，`YYYYMMDD` |
| 24 | 4 | period end，`YYYYMMDD` 排他上界 |
| 28 | 4 | flags；v2 必须为 0 |
| 32 | 4 | record 区 CRC32 |
| 36 | 4 | header CRC32；计算时本字段置 0 |
| 40 | 8 | 协议数据库指纹；不得为 0 |
| 48 | 16 | 保留，必须为 0 |

CRC 使用 reflected CRC-32 多项式 `0xedb88320`。

## 72 字节记录

| 偏移 | 大小 | 字段 |
|---:|---:|---|
| 0 | 1 | family：4 或 6 |
| 1 | 1 | mac_known：0 或 1 |
| 2 | 2 | protocol id：0–255 |
| 4 | 16 | IP；IPv4 后 12 字节必须为 0 |
| 20 | 6 | MAC；unknown 时必须全 0 |
| 26 | 6 | 保留，必须为 0 |
| 32 | 8 | connections |
| 40 | 8 | download bytes |
| 48 | 8 | download packets |
| 56 | 8 | upload bytes |
| 64 | 8 | upload packets |

加载器检查 CRC、精确长度、记录上限、family、保留字段、period 元数据和重复聚合 key。
period store 还会把文件中的协议数据库指纹与当前配置比较；不一致时拒绝启动或拒绝历史
查询，从而避免把旧的数字协议 ID 静默解释为另一个协议。

## 稳定协议 ID 与指纹

协议文件每条规则格式为：

```text
stable-id name l4-protocol port
20 HTTPS tcp 443
30 DNS udp 53
```

`stable-id` 范围为 1–255；0 固定为 `Other`。同一协议的多条端口规则必须显式使用同一
ID。配置顺序不参与指纹，因此单纯重排文件不会使数据库失效；ID、名称或规则映射变化会
改变指纹。不同名称不得复用同一 ID，同一名称也不得使用不同 ID。

## 提交与版本策略

提交先写同目录 `<name>.tmp`，执行 flush/fsync，再原子替换正式文件；POSIX 上尽力 fsync
父目录。v2 加载器拒绝 v1 和其他未知版本。项目尚未发布，因此本阶段直接修正格式，避免
长期携带已知的协议 ID 兼容性问题。
