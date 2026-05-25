# 聊天栏增强设计文档

## 概述
在 NEVO 客户端聊天栏中新增三个功能：Emoji 选择面板、图片上传、文件上传。

## 需求确认
- **图片/文件上传**：聊天内嵌展示 + 服务端存储（通过已有 FileUploadRequest 协议）
- **Emoji**：完整分类面板（6 个类别，60-80 个常用 emoji）
- **工具栏布局**：左侧排列 [😀] [📎] [🖼️]

## 架构设计

### 新增组件（views/ 目录）

| 文件 | 组件 | 职责 | 行数预估 |
|------|------|------|----------|
| `emoji_panel.py` | `EmojiPanel` | Emoji 分类选择器，弹出式面板 | ~180 |
| `file_upload_dialog.py` | `FileUploadDialog` | QFileDialog 封装，支持图片/文件类型过滤 | ~80 |
| `chat_input_bar.py` | `ChatInputBar` | 左侧工具栏 + 输入框 + Send 按钮 | ~120 |

### 修改组件

| 文件 | 修改内容 |
|------|----------|
| `chat_widget.py` | 委托输入给 ChatInputBar，新增图片/文件消息渲染（缩略图/文件卡片） |
| `nevo_client.py` | 新增 `send_image_chat()` / `send_file_chat()` 方法 |
| `main_window.py` | 连接新信号槽，处理上传请求回调 |

### 消息格式
- 图片消息：`[IMG:file_id]` 标记，解析为缩略图显示
- 文件消息：`[FILE:file_id:filename]` 标记，解析为文件卡片显示
- 文本消息：现有格式不变

### 上传流程
1. 用户点击 🖼️/📎 → FileUploadDialog 选择文件
2. 校验大小（max_file_size_mb）
3. 调用 `client.send_file_upload_request()` → 获得 file_id
4. 输入框自动插入 `[IMG:file_id]` 或 `[FILE:file_id:filename]`
5. 用户点击 Send → 消息广播到频道

### 组件信号

#### ChatInputBar
- `message_sent(str)` — 文本消息发送
- `emoji_requested()` — 需要弹出 Emoji 面板
- `file_requested()` — 需要打开文件选择器
- `image_requested()` — 需要打开图片选择器

#### EmojiPanel
- `emoji_selected(str)` — 用户选择了 emoji 字符
