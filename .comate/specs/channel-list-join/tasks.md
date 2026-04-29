# Channel List Display & Join Channel Feature - Task Plan

- [x] Task 1: Enhance ChannelTreeModel with current channel tracking and user counts
    - 1.1: Add custom role constants (ChannelIdRole, IsCurrentChannelRole, UserCountRole) and new data members (current_channel_id_, user_counts_, current_channel_icon_) to ChannelTreeModel.h
    - 1.2: Add public slots: setCurrentChannel(ChannelId), setChannelUserCount(ChannelId, int), clearCurrentChannel()
    - 1.3: Add leaveChannelRequested() signal
    - 1.4: Implement setCurrentChannel() - update current_channel_id_, emit dataChanged for old and new channel items
    - 1.5: Implement setChannelUserCount() - update user_counts_ map, emit dataChanged for the affected channel
    - 1.6: Implement clearCurrentChannel() - reset current_channel_id_ to invalid, emit dataChanged
    - 1.7: Enhance data() method to handle Qt::FontRole (bold for current), Qt::BackgroundRole (highlight for current), UserCountRole, IsCurrentChannelRole, and append user count to DisplayRole text
    - 1.8: Preserve current_channel_id_ across updateFromChannelList() calls

- [x] Task 2: Add currentChannelIcon to IconProvider
    - 2.1: Add static currentChannelIcon(int size = 20) declaration to IconProvider.h
    - 2.2: Implement currentChannelIcon() in IconProvider.cpp - same folder shape as channelIcon but with green accent color to indicate active/joined channel

- [x] Task 3: Create ChannelItemDelegate for rich channel rendering
    - 3.1: Create src/ui/include/nevo/ui/ChannelItemDelegate.h with QStyledItemDelegate subclass
    - 3.2: Create src/ui/src/ChannelItemDelegate.cpp with paint() override: draw left accent bar for current channel, draw user count badge, draw green dot for joined indicator
    - 3.3: Implement sizeHint() override to accommodate badge space

- [x] Task 4: Update MainWindow for join/leave interaction and edge-case handling
    - 4.1: Add onLeaveChannelRequested() private slot and loadMockChannelData() private method declarations to MainWindow.h
    - 4.2: Add duplicate-join guard in onJoinChannelRequested() - check if already in the target channel
    - 4.3: Add success feedback via status bar after successful join
    - 4.4: Implement onLeaveChannelRequested() - guard check, co_await leaveChannel(), error handling
    - 4.5: Update onChannelContextMenu() to show "Leave Channel" for current channel and "Join Channel" for others
    - 4.6: Wire onStateChanged callback to call channel_model_->setCurrentChannel() / clearCurrentChannel()
    - 4.7: Wire onUserJoined/onUserLeft to update channel user counts via channel_model_->setChannelUserCount()
    - 4.8: Implement loadMockChannelData() with sample channel hierarchy and user counts for standalone mode
    - 4.9: Call loadMockChannelData() in setupUi() when NEVO_HAS_BOOST is not defined
    - 4.10: Set ChannelItemDelegate on channel_tree_ in setupDockWidgets()

- [x] Task 5: Update CMakeLists.txt and fix code issues
    - 5.1: Add ChannelItemDelegate.cpp and ChannelItemDelegate.h to src/ui/CMakeLists.txt
    - 5.2: Fix duplicate code blocks in MainWindow.cpp (lines 200-206 duplicate lines 192-198, lines 770-837 duplicate lines 680-768)
