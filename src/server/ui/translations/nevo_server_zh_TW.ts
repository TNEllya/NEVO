<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_TW" sourcelanguage="en">
<context>
    <name>nevo::ServerMainWindow</name>
    <message><source>&amp;Server</source><translation>伺服器(&amp;S)</translation></message>
    <message><source>&amp;Start</source><translation>啟動(&amp;S)</translation></message>
    <message><source>S&amp;top</source><translation>停止(&amp;T)</translation></message>
    <message><source>Disconnect &amp;All</source><translation>中斷所有連線(&amp;A)</translation></message>
    <message><source>&amp;Quit</source><translation>結束(&amp;Q)</translation></message>
    <message><source>&amp;Help</source><translation>說明(&amp;H)</translation></message>
    <message><source>&amp;About</source><translation>關於(&amp;A)</translation></message>
    <message><source>Server running</source><translation>伺服器運行中</translation></message>
    <message><source>Server stopped</source><translation>伺服器已停止</translation></message>
    <message><source>Initialization Error</source><translation>初始化錯誤</translation></message>
    <message><source>About NEVO Server</source><translation>關於 NEVO 伺服器</translation></message>
    <message><source>NEVO VoIP Server</source><translation>NEVO 語音伺服器</translation></message>
    <message><source>A low-latency, encrypted VoIP server.</source><translation>低延遲、加密的語音伺服器。</translation></message>
    <message><source>Built with Qt 6, Boost.Asio, and SQLite3.</source><translation>基於 Qt 6、Boost.Asio 和 SQLite3 建構。</translation></message>
    <message><source>Confirm Exit</source><translation>確認結束</translation></message>
    <message><source>Server is still running. Stop server and exit?</source><translation>伺服器仍在運行。是否停止伺服器並結束？</translation></message>
    <message><source>Invalid Configuration</source><translation>設定無效</translation></message>
    <message><source>Configuration updated -- some changes will apply on next start</source><translation>設定已更新 — 部分變更將在下次啟動時生效</translation></message>
    <message><source>Configuration applied</source><translation>設定已套用</translation></message>
    <message><source>Configuration saved to %1</source><translation>設定已儲存到 %1</translation></message>
    <message><source>Save Error</source><translation>儲存錯誤</translation></message>
    <message><source>Failed to save configuration to %1</source><translation>無法將設定儲存到 %1</translation></message>
    <message><source>%1 - %2 clients | %3 channels</source><translation>%1 - %2 個用戶端 | %3 個頻道</translation></message>
    <message><source>&amp;Settings</source><translation>設定(&amp;S)</translation></message>
    <message><source>&amp;Language</source><translation>語言(&amp;L)</translation></message>
    <message><source>%1 - %2 clients</source><translation>%1 - %2 個用戶端</translation></message>
    <message><source>Owner Bound</source><translation>服主綁定成功</translation></message>
    <message><source>User %1 (ID: %2) has successfully bound as server owner.</source><translation>使用者 %1（ID: %2）已成功綁定為伺服器服主。</translation></message>
    <message><source>NEVO - No Server Owner</source><translation>NEVO - 伺服器無服主</translation></message>
    <message><source>This server has no owner (administrator).</source><translation>此伺服器尚無服主（管理員）。</translation></message>
    <message><source>Use the following bind key in the client to claim ownership.\n\nThis key is ONE-TIME USE and will be invalidated after binding.\nKeep it secret!</source><translation>請在用戶端中使用以下綁定金鑰認領服主身份。\n\n此金鑰為一次性使用，綁定後將自動失效。\n請妥善保管！</translation></message>
    <message><source>Copy to Clipboard</source><translation>複製到剪貼簿</translation></message>
</context>
<context>
    <name>nevo::ServerStatusBar</name>
    <message><source>NEVO Server</source><translation>NEVO 伺服器</translation></message>
    <message><source>Running</source><translation>運行中</translation></message>
    <message><source>Stopped</source><translation>已停止</translation></message>
    <message><source>Clients: %1 / %2 auth</source><translation>用戶端：%1 / %2 已認證</translation></message>
    <message><source>Clients: 0 / 0 auth</source><translation>用戶端：0 / 0 已認證</translation></message>
    <message><source>Relayed: %1</source><translation>已中繼：%1</translation></message>
    <message><source>Relayed: 0</source><translation>已中繼：0</translation></message>
    <message><source>Uptime: %1:%2:%3</source><translation>運行時間：%1:%2:%3</translation></message>
    <message><source>Uptime: 00:00:00</source><translation>運行時間：00:00:00</translation></message>
</context>
<context>
    <name>nevo::ServerConfigPanel</name>
    <message><source>Server Configuration</source><translation>伺服器設定</translation></message>
    <message><source>Network</source><translation>網路</translation></message>
    <message><source>Server Name:</source><translation>伺服器名稱：</translation></message>
    <message><source>Enter server name</source><translation>輸入伺服器名稱</translation></message>
    <message><source>TCP Port:</source><translation>TCP 埠：</translation></message>
    <message><source>* Restart required</source><translation>* 需要重啟</translation></message>
    <message><source>UDP Port:</source><translation>UDP 埠：</translation></message>
    <message><source>Advanced</source><translation>進階</translation></message>
    <message><source>Max Users:</source><translation>最大使用者數：</translation></message>
    <message><source>Welcome Message:</source><translation>歡迎訊息：</translation></message>
    <message><source>Welcome message for new users</source><translation>新使用者的歡迎訊息</translation></message>
    <message><source>Log Level:</source><translation>日誌層級：</translation></message>
    <message><source>IO Threads:</source><translation>IO 執行緒數：</translation></message>
    <message><source>Apply</source><translation>套用</translation></message>
    <message><source>Save Config</source><translation>儲存設定</translation></message>
    <message><source>Server is running — port and thread changes will take effect after restart</source><translation>伺服器運行中 — 埠和執行緒變更將在重啟後生效</translation></message>
</context>
<context>
    <name>nevo::SessionTableModel</name>
    <message><source>Muted</source><translation>已靜音</translation></message>
    <message><source>Speaking</source><translation>發言中</translation></message>
    <message><source>Idle</source><translation>閒置</translation></message>
    <message><source>User ID</source><translation>使用者 ID</translation></message>
    <message><source>Username</source><translation>使用者名稱</translation></message>
    <message><source>Address</source><translation>位址</translation></message>
    <message><source>Channel</source><translation>頻道</translation></message>
    <message><source>Status</source><translation>狀態</translation></message>
</context>
<context>
    <name>nevo::ChannelPanel</name>
    <message><source>Channels</source><translation>頻道</translation></message>
    <message><source>Channel</source><translation>頻道</translation></message>
    <message><source>Add</source><translation>新增</translation></message>
    <message><source>Delete</source><translation>刪除</translation></message>
    <message><source>Rename</source><translation>重新命名</translation></message>
    <message><source>Error</source><translation>錯誤</translation></message>
    <message><source>Server is not running</source><translation>伺服器未運行</translation></message>
    <message><source>Add Channel</source><translation>新增頻道</translation></message>
    <message><source>Channel name:</source><translation>頻道名稱：</translation></message>
    <message><source>Channel manager not available</source><translation>頻道管理員不可用</translation></message>
    <message><source>Channel '%1' created</source><translation>頻道 '%1' 已建立</translation></message>
    <message><source>Info</source><translation>提示</translation></message>
    <message><source>Please select a channel to delete</source><translation>請選擇要刪除的頻道</translation></message>
    <message><source>Cannot delete the Root or Lobby channel</source><translation>無法刪除根頻道或大廳頻道</translation></message>
    <message><source>Delete Channel</source><translation>刪除頻道</translation></message>
    <message><source>Are you sure you want to delete channel '%1'?
All sub-channels will also be deleted.
Users in these channels will be moved to Lobby.</source><translation>確定要刪除頻道 '%1' 嗎？
所有子頻道也將被刪除。
這些頻道中的使用者將被移至大廳。</translation></message>
    <message><source>Channel '%1' deleted</source><translation>頻道 '%1' 已刪除</translation></message>
    <message><source>Please select a channel to rename</source><translation>請選擇要重新命名的頻道</translation></message>
    <message><source>Cannot rename the Root channel</source><translation>無法重新命名根頻道</translation></message>
    <message><source>Rename Channel</source><translation>重新命名頻道</translation></message>
    <message><source>New channel name:</source><translation>新頻道名稱：</translation></message>
    <message><source>Channel renamed from '%1' to '%2'</source><translation>頻道已從 '%1' 重新命名為 '%2'</translation></message>
</context>
</TS>
