<!-- 负责人：成员1：服务端架构/组长 -->
# 第一版网络协议

所有整数均为无符号大端序（网络字节序）。禁止直接发送 C++ 结构体。

## 固定报文头（24 字节）

| 偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 4 | magic | `0x4C434431`，ASCII `LCD1` |
| 4 | 2 | version | 第一版为 `1` |
| 6 | 2 | type | `MessageType` |
| 8 | 4 | flags | bit0 RESPONSE、bit1 ERROR、bit2 BINARY、bit3 FINAL、bit4 PUSH |
| 12 | 4 | bodyLength | 消息体字节数，绝对上限 4 MiB |
| 16 | 8 | requestId | 客户端非零递增 ID，响应必须原样回显 |

控制体为 UTF-8 JSON；文件块为二进制。第一版在一条控制连接上顺序执行请求，因此客户端发送一个请求后读取一个响应。第二版会把传输迁移到独立任务连接。

## 消息类型

| 数值 | 消息 |
|---:|---|
| 1 / 2 / 3 | HELLO / HEARTBEAT / ERROR_RESP |
| 100～105 | REGISTER、LOGIN、LOGOUT 请求/响应 |
| 200～207 | LIST、MKDIR、RENAME、DELETE 请求/响应 |
| 300～305 | UPLOAD_INIT、UPLOAD_CHUNK/ACK、UPLOAD_FINISH |
| 400～403 | DOWNLOAD_INIT、DOWNLOAD_CHUNK_REQ/CHUNK |
| 500～503 | SHARE_CREATE / SHARE_CLAIM 请求/响应 |
| 600～601 | CONVERT_REQ / CONVERT_RESP |

失败统一响应 `ERROR_RESP`，设置 `RESPONSE|ERROR`，并返回：

```json
{"originalType":102,"errorCode":"UNAUTHORIZED","message":"invalid username or password"}
```

## 上传

1. `UPLOAD_INIT_REQ`：`token,parentId,name,size,sha256`；
2. 若内容已存在，响应 `instant=true,nodeId`；
3. 否则响应 32 字节十六进制 `transferId` 和 `chunkSize`；
4. `UPLOAD_CHUNK` 二进制体布局：

| 偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 64 | 会话 token 的 ASCII 十六进制文本 |
| 64 | 32 | transferId 的 ASCII 十六进制文本 |
| 96 | 8 | offset，大端序 |
| 104 | 余下 | 原始文件块 |

5. 服务端要求 `offset == 已接收字节数`；
6. `UPLOAD_FINISH_REQ` 触发大小与 SHA-256 复核及原子提交。

## 下载

1. `DOWNLOAD_INIT_REQ`：`token,nodeId`；
2. 响应 `transferId,name,size,sha256,chunkSize`；
3. 客户端循环发送 `DOWNLOAD_CHUNK_REQ`：`token,transferId,offset,maxBytes`；
4. 服务端返回 `DOWNLOAD_CHUNK`，设置 `RESPONSE|BINARY`，最后一块额外设置 `FINAL`；
5. 客户端写 `.download` 并校验，成功后才改为目标名称。

## Word/PDF 转 Markdown

客户端发送 `CONVERT_REQ`：

```json
{"token":"...","nodeId":12,"outputName":"报告.md"}
```

`outputName` 为空时，服务端使用源文件主名称加 `.md`。服务端仅接受 `.docx` 和
文字型 `.pdf`，在本地调用随发布包携带的 MarkItDown 转换组件，并将结果作为普通
网盘文件写入源文件所在目录。成功响应 `CONVERT_RESP`：

```json
{"nodeId":13,"name":"报告.md"}
```

转换是同步操作；源文件最大 100 MiB，转换结果最大 64 MiB，后台转换最长运行 120 秒。

## 限制

- JSON 控制体最大 1 MiB，任意消息体最大 4 MiB；
- 默认块 256 KiB；
- 用户名 3～32 UTF-8 字节，口令 8～128 UTF-8 字节；
- 虚拟名称 1～255 UTF-8 字节，拒绝控制字符、`/`、`\`、`.` 和 `..`；
- 第一版尚未实现独立任务连接、取消、重试、心跳超时和断点续传。
