# Join Channel UI & Microphone Input Monitor — Task Plan

- [x] Task 1: Add Join/Leave Channel Buttons to Channel Dock
    - 1.1: Add member variables to MainWindow.h: `QPushButton* join_channel_btn_`, `QPushButton* leave_channel_btn_`
    - 1.2: Add slot `onChannelSelectionChanged()` to MainWindow.h
    - 1.3: Modify `setupDockWidgets()` in MainWindow.cpp: replace `channel_dock_->setWidget(channel_tree_)` with a QVBoxLayout container holding tree view + horizontal button row
    - 1.4: Style join/leave buttons consistently with existing dark theme
    - 1.5: Connect `channel_tree_->selectionModel()->selectionChanged` to `onChannelSelectionChanged()`
    - 1.6: Connect `join_channel_btn_->clicked` to `onJoinChannelRequested()` with selected channel ID
    - 1.7: Connect `leave_channel_btn_->clicked` to `onLeaveChannelRequested()`
    - 1.8: Implement `onChannelSelectionChanged()`: enable join button if a different channel is selected and connected; enable leave button if currently in a channel

- [x] Task 2: Add Current Channel Label to User Dock
    - 2.1: Add member `QLabel* current_channel_label_` to MainWindow.h
    - 2.2: Modify `setupDockWidgets()`: add a QLabel at top of user dock above the QListView, styled with dark theme accent
    - 2.3: Update `onJoinChannelRequested()` success path: set label to `tr("Channel: %1").arg(channel_name)` with green accent
    - 2.4: Update `onLeaveChannelRequested()` success path: set label to `tr("Not in a channel")` with muted style
    - 2.5: Update existing state change handler to refresh current channel label based on ClientCore state

- [x] Task 3: Add Microphone VU Meter to Toolbar
    - 3.1: Add members to MainWindow.h: `QProgressBar* mic_level_bar_`, `QLabel* mic_icon_label_`, `QTimer* mic_level_timer_`
    - 3.2: Add slot `onMicLevelTimeout()` to MainWindow.h
    - 3.3: In toolbar setup, add separator + mic icon label + QProgressBar (120px wide, 14px tall, no text) to the right side of `top_toolbar_`
    - 3.4: Style VU meter: dark background, green chunk, rounded corners
    - 3.5: Hide mic icon and VU meter initially (shown only when connected)
    - 3.6: Create `mic_level_timer_` at 33ms interval, connect timeout to `onMicLevelTimeout()`
    - 3.7: Implement `onMicLevelTimeout()`: poll `client_core_->getCurrentInputLevel()`, update bar value, dynamically change chunk color (green < 60%, orange 60-85%, red > 85%), update mic icon color based on mute state

- [x] Task 4: Wire VU Meter to Connection State
    - 4.1: In the connection state change handler (where `updateConnectionState` is called): when state is Connected or InChannel, show mic widgets and start timer; when Disconnected or Connecting, stop timer and hide widgets
    - 4.2: Ensure `client_core_` audio is initialized before starting VU meter polling (guard with null check)
    - 4.3: Reset VU meter bar to 0 when stopping

- [x] Task 5: Update retranslateUi() and Translation Files
    - 5.1: Add new `tr()` calls to `retranslateUi()`: join/leave button text, current channel label text, mic icon tooltip
    - 5.2: Update `nevo_client_en.ts` with new source strings
    - 5.3: Update `nevo_client_zh_CN.ts` with Simplified Chinese translations
    - 5.4: Update `nevo_client_zh_TW.ts` with Traditional Chinese translations

- [x] Task 6: Build Verification
    - 6.1: Run full build and fix any compilation errors
    - 6.2: Verify channel dock shows join/leave buttons
    - 6.3: Verify user dock shows current channel label
    - 6.4: Verify toolbar shows microphone VU meter when connected
