# 第一版数据库与文件存储

服务端使用 SQLite，启动时开启外键和 WAL，并自动创建以下表。

## users

| 字段 | 说明 |
|---|---|
| `id` | 用户 ID |
| `username` | 唯一用户名 |
| `password_hash` / `salt` | 演示级随机盐 SHA-256；第二版改 PBKDF2 |
| `created_at` | Unix 毫秒 |

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
- 数据库操作在第一版由仓储互斥锁串行化，适合课程演示规模。
