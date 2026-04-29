# Feature: Join Channel UI & Microphone Input Monitor

## Requirement Scenario

### 1. Join Channel (加入频道)

**Current state**: Channel joining works via double-click on the channel tree or right-click context menu. However, there is no prominent "Join" / "Leave" button in the main UI, and the current channel is not clearly displayed outside the tree highlight.

**Target**:
- Add a "Join Channel" button below the channel tree that becomes active when a channel is selected and the user is not already in it
- Add a "Leave Channel" button that appears when the user is in a channel
- Add a current channel info bar at the top of the user list dock showing the channel name the user is currently in
- Status bar message updates on join/leave success/failure

### 2. Microphone Input Monitoring (麦克风输入监听测试)

**Current state**: Input level monitoring exists only inside the AudioSettingsWidget modal dialog, and only after clicking "Test Input". There is no persistent microphone level indicator in the main window.

**Target**:
- Add a persistent microphone VU meter bar in the top toolbar area, always visible when connected
- The VU meter shows real-time input level using `AudioEngine::getCurrentInputLevel()` polled at ~30fps
- A small microphone icon next to the VU meter that visually indicates mute state
- The VU meter is hidden when not connected, shown when connected/in-channel

---

## Architecture & Technical Approach

### Join Channel UI

**Channel action buttons** will be placed in the channel dock below the tree view:
- Replace the current `channel_dock_` widget (which is just `QTreeView`) with a `QWidget` containing a `QVBoxLayout`: tree on top, a horizontal button row at bottom
- Button row: "Join Channel" button + "Leave Channel" button
- Buttons are enabled/disabled based on selection and connection state

**Current channel info bar** will be a `QLabel` inserted at the top of the user dock:
- When not in a channel: shows "Not in a channel"
- When in a channel: shows channel name with a colored indicator

### Microphone Input Monitor

**VU meter widget** in the toolbar:
- A `QProgressBar` (horizontal, small height, no text) added to the top toolbar
- A `QLabel` with a microphone icon (Unicode or custom) before the bar
- A `QTimer` at ~33ms interval polls `ClientCore::getCurrentInputLevel()` and updates the bar
- Timer starts when connected, stops when disconnected
- The VU meter is styled with green/yellow/red gradient based on level

---

## Affected Files

### Modified files:

1. **`src/ui/include/nevo/ui/MainWindow.h`**
   - Add members: `QPushButton* join_channel_btn_`, `QPushButton* leave_channel_btn_`, `QLabel* current_channel_label_`, `QProgressBar* mic_level_bar_`, `QLabel* mic_icon_label_`, `QTimer* mic_level_timer_`, `QVBoxLayout* channel_layout_`
   - Add slots: `onChannelSelectionChanged()`, `onMicLevelTimeout()`

2. **`src/ui/src/MainWindow.cpp`**
   - Modify `setupUi()`: add VU meter to toolbar
   - Modify `setupDockWidgets()`: add join/leave buttons below channel tree, add current channel label above user list
   - Add `onChannelSelectionChanged()`: enable/disable join/leave based on selection
   - Modify `onJoinChannelRequested()`: update current channel label and buttons
   - Modify `onLeaveChannelRequested()`: update current channel label and buttons
   - Add `onMicLevelTimeout()`: poll input level and update VU meter
   - Update connection state change handler: show/hide VU meter, start/stop timer
   - Update `retranslateUi()`: add new translatable strings

3. **`src/ui/translations/nevo_client_en.ts`** — Add new source strings
4. **`src/ui/translations/nevo_client_zh_CN.ts`** — Add Chinese translations
5. **`src/ui/translations/nevo_client_zh_TW.ts`** — Add Chinese translations

---

## Implementation Details

### 1. Channel Dock Layout Change

Current: `channel_dock_->setWidget(channel_tree_)`

New:
```cpp
// Container widget with vertical layout
QWidget* channel_container = new QWidget(channel_dock_);
QVBoxLayout* channel_layout = new QVBoxLayout(channel_container);
channel_layout->setContentsMargins(0, 0, 0, 0);
channel_layout->setSpacing(4);

// Tree view (existing)
channel_tree_ = new QTreeView(channel_container);
// ... existing tree setup ...
channel_layout->addWidget(channel_tree_, 1);

// Button row
QHBoxLayout* btn_layout = new QHBoxLayout();
btn_layout->setContentsMargins(8, 4, 8, 4);

join_channel_btn_ = new QPushButton(tr("Join Channel"), channel_container);
join_channel_btn_->setEnabled(false);
join_channel_btn_->setStyleSheet(/* styled button */);

leave_channel_btn_ = new QPushButton(tr("Leave Channel"), channel_container);
leave_channel_btn_->setEnabled(false);
leave_channel_btn_->setStyleSheet(/* styled button */);

btn_layout->addWidget(join_channel_btn_, 1);
btn_layout->addWidget(leave_channel_btn_, 1);
channel_layout->addLayout(btn_layout);

channel_dock_->setWidget(channel_container);
```

### 2. Current Channel Label in User Dock

```cpp
// At top of user dock, above the user list
current_channel_label_ = new QLabel(tr("Not in a channel"), user_dock_);
current_channel_label_->setStyleSheet(
    "color: #a0a8b8; padding: 6px 8px; font-size: 12px; "
    "background-color: #1e2227; border-bottom: 1px solid #2c3138;");
```

When user joins a channel:
```cpp
current_channel_label_->setText(tr("Channel: %1").arg(channel_name));
current_channel_label_->setStyleSheet(/* with green accent */);
```

### 3. Microphone VU Meter in Toolbar

```cpp
// In setupMenuBar() / toolbar setup area:
mic_icon_label_ = new QLabel(QString::fromUtf8("\xF0\x9F\x8E\xA4"), this);  // 🎤 or use "MIC"
mic_icon_label_->setStyleSheet("color: #a0a8b8; font-size: 14px;");

mic_level_bar_ = new QProgressBar(this);
mic_level_bar_->setRange(0, 100);
mic_level_bar_->setValue(0);
mic_level_bar_->setFixedWidth(120);
mic_level_bar_->setFixedHeight(14);
mic_level_bar_->setTextVisible(false);
mic_level_bar_->setStyleSheet(
    "QProgressBar { background: #1e2227; border: 1px solid #3a3f4b; border-radius: 3px; }"
    "QProgressBar::chunk { background: #4caf50; border-radius: 2px; }");

top_toolbar_->addSeparator();
top_toolbar_->addWidget(mic_icon_label_);
top_toolbar_->addWidget(mic_level_bar_);

// Hidden initially, shown when connected
mic_icon_label_->hide();
mic_level_bar_->hide();
```

Timer-based polling:
```cpp
mic_level_timer_ = new QTimer(this);
mic_level_timer_->setInterval(33);  // ~30fps
connect(mic_level_timer_, &QTimer::timeout, this, &MainWindow::onMicLevelTimeout);

void MainWindow::onMicLevelTimeout() {
    if (!client_core_) return;
    float level = client_core_->getCurrentInputLevel();
    int value = static_cast<int>(level * 100.0f);
    mic_level_bar_->setValue(value);

    // Color based on level
    QString color = value < 60 ? "#4caf50" : value < 85 ? "#ff9800" : "#f44336";
    mic_level_bar_->setStyleSheet(
        "QProgressBar { background: #1e2227; border: 1px solid #3a3f4b; border-radius: 3px; }"
        "QProgressBar::chunk { background: " + color + "; border-radius: 2px; }");

    // Update mic icon based on mute state
    bool muted = client_core_->isMuted();
    mic_icon_label_->setStyleSheet(muted
        ? "color: #f44336; font-size: 14px;"
        : "color: #4caf50; font-size: 14px;");
}
```

Start/stop with connection state:
```cpp
// When state changes to Connected/InChannel:
mic_icon_label_->show();
mic_level_bar_->show();
mic_level_timer_->start();

// When state changes to Disconnected/Connecting:
mic_level_timer_->stop();
mic_icon_label_->hide();
mic_level_bar_->hide();
```

---

## Boundary Conditions & Exception Handling

1. **Join channel while not connected**: Button disabled, no action
2. **Join same channel again**: No-op, show informational message
3. **Leave channel while not in channel**: Leave button disabled
4. **AudioEngine not initialized**: `getCurrentInputLevel()` returns 0.0f (already handled)
5. **VU meter timer during disconnect**: Timer stopped, bar hidden
6. **Channel selection changed while in channel**: Join button disabled for current channel, enabled for others
7. **Mute state change**: Mic icon color updates on next timer tick

---

## Data Flow Paths

### Join Channel
```
User clicks "Join Channel" button
  → MainWindow::onJoinChannelRequested(selected_channel_id)
    → client_core_->joinChannel(channel_id) (coroutine)
      → NetworkManager::sendControl(JoinChannel)
        → Server processes and responds
      → ClientCore updates state to InChannel
    → MainWindow updates UI:
      - current_channel_label_ shows channel name
      - leave_channel_btn_ enabled
      - join_channel_btn_ disabled (for current channel)
      - ChannelTreeModel highlights current channel
      - Status bar: "Joined channel: ..."
```

### Microphone Monitor
```
AudioEngine capture callback (real-time thread)
  → Computes peak level → stores in std::atomic<float>
  
MainWindow::onMicLevelTimeout() (main thread, 30fps)
  → ClientCore::getCurrentInputLevel()
    → AudioEngine::getCurrentInputLevel()
      → Reads atomic<float>
  → Updates QProgressBar value and color
  → Updates mic icon mute state
```

---

## Expected Outcomes

1. Users can join/leave channels via prominent buttons, not just double-click/context menu
2. Current channel name is clearly displayed in the user list dock header
3. A persistent microphone VU meter in the toolbar shows real-time input level
4. VU meter color changes from green (normal) to orange (loud) to red (clipping)
5. Mic icon turns red when muted, green when unmuted
6. All new UI strings are translatable via tr()
