# NEVO 协议线格式规范（唯一权威定义）

> 本文档是 NEVO 各端控制通道线格式的**唯一权威定义**。
> 所有实现（C++ 服务端/客户端、Python 客户端、Android 客户端、Web 网关）必须与本文档一致。
> 跨语言一致性由金样互操作测试锁定，格式变更流程见文末。

## 1. 适用范围

| 通道 | 线格式 | 说明 |
|------|--------|------|
| TCP 控制通道 | 自定义小端 TLV（本文档） | 四端统一，运行时唯一格式 |
| UDP 语音通道 | Protobuf `VoicePacketHeader` + 加密载荷 | 四端统一（protobuf 生成代码） |
| UDP 视频通道 | Protobuf `VideoPacketHeader` + 加密载荷 | 四端统一（protobuf 生成代码） |
| 管理面 IPC | JSON（换行分隔） | 仅 ControlServer ↔ GUI/Web 管理端 |

> `PacketCodec` 中的 `encodeTcpFrame`/`decodeTcpFramePayload`（Protobuf TCP 帧）
> 为 **legacy/测试用途**，生产运行时未使用，不得在新增代码中引用。

## 2. TCP 帧结构

```
[0..3]   payload_length : uint32 大端 —— 载荷字节数（不含帧头）
[4..7]   message_type   : uint32 大端 —— ControlMessageType / MessageType 枚举值
[8..11]  request_id     : uint32 大端 —— 请求-响应关联 ID
[12..]   payload        : 见第 3 节
```

帧头编解码：C++ `TcpConnection`（boost::endian 大端）、Python `nevo_client.py`
（`struct.pack('>III')`）、Android `TcpConnectionManager.kt`（12 字节大端）——三端一致。

## 3. 控制载荷（payload）结构

```
[0..3]   case_value     : uint32 小端 —— 消息类型（= MessageType 枚举值 = protobuf oneof 字段号）
[4..7]   inner_len      : uint32 小端 —— 内层 TLV 载荷字节数
[8..]    inner_payload  : 内层 TLV（见下）
```

### 3.1 内层 TLV 编码

所有多字节整数均为**小端**（LE）：

| 类型 | 编码 |
|------|------|
| string | `[u32 LE 字节长度][UTF-8 数据]` |
| bytes | `[u32 LE 字节长度][原始数据]` |
| uint16 | `[2B LE]` |
| uint32 | `[4B LE]` |
| uint64 | `[8B LE]` |
| bool | `[1B]`（0/1） |
| 列表 | `[u32 LE 元素个数][元素1][元素2]...` |

### 3.2 字段顺序

每个消息的字段顺序与其在 `proto/control.proto` 中对应 message 的字段**声明顺序一致**。

示例（LoginRequest，case=1）：

```
username            : string
auth_credential     : bytes
key_exchange_methods: u32 count + string*
client_public_key   : bytes
client_udp_port     : u16
client_video_udp_port: u16（仅当非 0 时写入）
```

## 4. 实现对照（必须保持同步）

| 层 | 位置 |
|----|------|
| C++ 编解码 | `src/core/src/protocol/PacketCodec.cpp`：`encodeCustomWirePayload` / `decodeCustomWirePayload` + `CASE_ENCODERS` / case 解码表 |
| Python 编解码 | `src/client/gui_python/nevo_wire.py`：`serialize_*` / `deserialize_*` + `MESSAGE_TYPE_MAP` / `CASE_TO_DESERIALIZER` |
| Android 编解码 | `mobile/android/app/src/main/java/com/nevo/voip/core/protocol/ProtocolSerializer.kt`：`NevoBuffer`（LITTLE_ENDIAN）+ `MESSAGE_TYPE_MAP` / `CASE_TO_DESERIALIZER` |
| 消息类型枚举 | `proto/control.proto`（真源）、`src/core/include/nevo/core/protocol/PacketTypes.h`、Python `MessageType`、Android `MessageType.kt`（三者必须与 proto 编号一致） |

## 5. 金样互操作测试

格式一致性由两端金样测试锁定（同一组硬编码字节）：

- **Python → C++**：`tests/core_tests/TestPacketCodec.cpp::PacketCodecInteropTest.DecodePythonLoginRequestGolden`（C++ 解码 Python 编码的 LoginRequest）
- **C++ → Python**：`tests/core_tests/TestPacketCodec.cpp::PacketCodecInteropTest.EncodeLoginResponseMatchesPythonGolden`（C++ 编码必须与 Python 编码逐字节一致）
- **Python 侧**：`src/client/gui_python/tests/test_wire_format.py::WireFormatGoldenTest`

## 6. 格式变更流程（强制）

1. 修改 `proto/control.proto`（只允许在 message 末尾**追加**字段，编号只增不改）
2. 同步更新 C++ `PacketCodec.cpp` 编解码器
3. 同步更新 Python `nevo_wire.py` 编解码器
4. 同步更新 Android `ProtocolSerializer.kt` 编解码器
5. 更新本文件与金样测试（重新生成金样字节并替换两处测试）
6. 全量构建 + `ctest` + `python -m unittest tests.test_wire_format` 全绿后方可提交
