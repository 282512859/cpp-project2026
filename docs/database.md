<!-- 负责人：成员2：服务端存储 -->
# 数据库与文件存储

服务端使用 SQLite，启动时开启外键和 WAL，并自动创建以下表。

当前文档同时描述基础实现和 `CloudRepository_hardened.cpp` 加固副本。
加固副本保留原有数据库结构，并补充了配额、并发领取和失败清理逻辑。
由于它目前尚未加入 CMake 的 `cloud_server` 源文件列表，正式服务仍默认编译
`CloudRepository.cpp`；接入副本后才会启用本文中的 PBKDF2 和相关修复。

## users

| 字段 | 说明 |
|---|---|
| `id` | 用户 ID |
| `username` | 唯一用户名 |
| `password_hash` / `salt` | 加固副本使用 PBKDF2-HMAC-SHA-256；旧版 SHA-256 账号登录成功后自动升级 |
| `created_at` | Unix 毫秒 |

### 密码存储格式

加固副本使用随机盐和 600000 次 PBKDF2-HMAC-SHA-256 派生，
`password_hash` 的格式为：

```text
pbkdf2_sha256$600000$<derived-key-hex>
```

`salt` 仍单独保存在 `users.salt` 中。登录时根据格式重新派生密码，
并使用固定时间比较；第一版格式 `sha256(salt + password)` 仍可验证，
但成功登录后会被替换为 PBKDF2 格式。这样不需要一次性修改全部用户记录。

## nodes

文件和目录共用节点树。根目录不建记录，协议统一用 `parent_id=0` 表示。`(owner_id,parent_id,name)` 唯一，所有查询必须同时带 `owner_id`。

| 字段 | 说明 |
|---|---|
| `id, owner_id, parent_id` | 节点及归属关系 |
| `name` | 仅为虚拟名称，不拼服务端磁盘路径 |
| `is_directory` | 1 为目录，0 为文件 |
| `blob_id` | 文件引用的内容实体；目录为空 |
| `size, modified_at` | 大小和修改时间 |

## blobs

`(sha256,size)` 唯一。物理路径由 SHA-256 生成：`blobs/ab/cd/<sha256>`。多个节点可引用同一个 blob，`ref_count` 记录引用数；最后一个引用删除后才移除磁盘实体。

## 一致性规则

- 上传初始化时只创建 `.part`，不创建 `nodes`；
- 完成时服务端重新计算临时文件 SHA-256；
- blob 引用增加和 node 插入位于同一事务；
- 删除目录使用递归 CTE 删除后代并减少 blob 引用；
- 所有 SQL 使用预处理参数；
- 数据库操作在第一版由仓储互斥锁串行化，适合课程演示规模；
- 分享领取时，增加 blob 引用、创建 node、更新领取者配额和标记分享码在同一事务中完成；
- 分享码更新要求影响行数为 1，避免同一分享码被重复领取；
- 配额计算在加法前检查负数和整数溢出；
- 上传提交失败时，会补偿删除已移动但尚未被 node 引用的物理文件；
- 数据库路径没有父目录时，不对空路径调用 `create_directories()`。

## 配额统计规则

当前逻辑配额按用户文件节点的逻辑大小统计，而不是按物理 blob 去重后的磁盘大小统计：

- 上传一个文件：`storage_used += 文件大小`；
- 领取一个分享文件：`storage_used += 文件大小`，但不复制物理 blob；
- 删除文件节点：`storage_used -= 文件大小`；
- `quota <= 0` 表示该用户不限制配额。

因此，多个用户引用同一个 blob 时，磁盘只保存一份内容，但每个用户仍按自己的文件节点占用配额。
