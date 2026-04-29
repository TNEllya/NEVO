# Channel List Display & Join Channel Feature - Spec Document

## 1. Requirement Analysis

### Current State
NEVO is a C++20/Qt 6 VoIP desktop client. The existing codebase already includes:
- `ChannelTreeModel` (QAbstractItemModel) displaying a tree of channels in a left-side `QDockWidget`
- `ChannelInfo` struct with `channel_id`, `name`, `parent_id`, `is_permanent`
- Double-click and right-click context menu to join a channel
- `MainWindow::onJoinChannelRequested()` that calls `ClientCore::joinChannel()` via Boost.Asio coroutine
- `ClientCore` tracking `current_channel_` and `current_channel_name_`
- `onChannelList` callback that updates `ChannelTreeModel` from server data

### Gaps to Fill
1. **No visual indicator for currently joined channel** - The tree does not highlight which channel the user is in. `ClientCore` tracks `current_channel_` but `ChannelTreeModel` is unaware.
2. **No "joined vs. not-joined" visual distinction** - All channels look identical regardless of join status.
3. **No mock/demo data in standalone mode** - When built without Boost (`NEVO_HAS_BOOST`), the channel tree is empty, making it impossible to test or demonstrate the UI.
4. **No success feedback on join** - Failure shows QMessageBox, but success gives no visual cue.
5. **No duplicate-join prevention** - Clicking "Join Channel" on the already-joined channel re-sends the request.
6. **No "Leave Channel" interaction** - No UI way to leave the current channel.
7. **No user count in channel items** - Channel items only show name + icon; no user count or activity indicator.

### Requirement Summary
Enhance the existing channel list display and join channel flow to provide:
- Visual distinction between joined/unjoined channels
- Current channel highlighting with user count
- Mock data for standalone (no-Boost) mode
- Proper join/leave feedback and edge-case handling

## 2. Architecture & Technical Approach

### Data Flow
```
Server/ClientCore  ──ChannelList callback──>  ChannelTreeModel.updateFromChannelList()
                                              └─ builds tree from flat ChannelInfo list

ClientCore  ──onStateChanged(InChannel)──>  MainWindow  ──setCurrentChannel()──>  ChannelTreeModel
                                                                        └─ marks current channel
                                                                        └─ triggers dataChanged()

User click/double-click  ──>  ChannelTreeModel.joinChannelRequested  ──>  MainWindow.onJoinChannelRequested()
                                                                            └─ guard: already in channel?
                                                                            └─ ClientCore::joinChannel()
                                                                            └─ success: update UI
                                                                            └─ failure: QMessageBox
```

### Approach
1. Extend `ChannelTreeModel` to track `current_channel_id_` and a map of `user_counts_` per channel.
2. Add a custom `ChannelItemDelegate` (QStyledItemDelegate) for rich rendering: bold text + colored icon for current channel, user count badge.
3. Add `setCurrentChannel()`, `setChannelUserCount()` public slots to `ChannelTreeModel`.
4. Wire `ClientCore::onStateChanged` (when `InChannel`) to call `ChannelTreeModel::setCurrentChannel()`.
5. Wire `ClientCore::onUserJoined`/`onUserLeft` to update user counts via `ChannelTreeModel::setChannelUserCount()`.
6. Add "Leave Channel" to the context menu for the current channel.
7. Add duplicate-join guard in `MainWindow::onJoinChannelRequested()`.
8. Add status bar feedback on successful join/leave.
9. Add mock data injection in `MainWindow::setupUi()` for standalone mode.

## 3. Affected Files

| File | Type | Description |
|------|------|-------------|
| `src/ui/include/nevo/ui/ChannelTreeModel.h` | Modify | Add `current_channel_id_`, `user_counts_`, new slots/signals |
| `src/ui/src/ChannelTreeModel.cpp` | Modify | Implement new data roles, current channel rendering, user count |
| `src/ui/include/nevo/ui/ChannelItemDelegate.h` | **New** | Custom delegate for rich channel item rendering |
| `src/ui/src/ChannelItemDelegate.cpp` | **New** | Paint logic: bold current channel, user count badge, joined indicator |
| `src/ui/include/nevo/ui/MainWindow.h` | Modify | Add leave channel slot, mock data method |
| `src/ui/src/MainWindow.cpp` | Modify | Wire new signals, add guards, add mock data, update context menu |
| `src/ui/CMakeLists.txt` | Modify | Add new source files |

## 4. Implementation Details

### 4.1 ChannelTreeModel Enhancements

**Header additions** (`ChannelTreeModel.h`):
```cpp
// New custom role for channel state
constexpr int ChannelIdRole = Qt::UserRole + 1;
constexpr int IsCurrentChannelRole = Qt::UserRole + 2;
constexpr int UserCountRole = Qt::UserRole + 3;

// In ChannelTreeModel class:
public slots:
    void setCurrentChannel(ChannelId channel_id);
    void setChannelUserCount(ChannelId channel_id, int count);
    void clearCurrentChannel();

signals:
    void leaveChannelRequested();  // Emitted when user leaves current channel

private:
    ChannelId current_channel_id_;  // Currently joined channel
    std::unordered_map<ChannelId, int> user_counts_;  // User count per channel
    QIcon current_channel_icon_;     // Icon variant for current channel
```

**Data method enhancement** (`ChannelTreeModel.cpp`):
```cpp
QVariant ChannelTreeModel::data(const QModelIndex& index, int role) const
{
    // ... existing code ...
    switch (role) {
        case Qt::DisplayRole:
            // Append user count if > 0
            name = item->name();
            auto it = user_counts_.find(item->channelId());
            if (it != user_counts_.end() && it->second > 0) {
                return QString("%1 (%2)").arg(QString::fromStdString(name)).arg(it->second);
            }
            return QString::fromStdString(name);

        case Qt::DecorationRole:
            if (item->channelId() == current_channel_id_) {
                return current_channel_icon_;
            }
            return channel_icon_;

        case Qt::FontRole:
            if (item->channelId() == current_channel_id_) {
                QFont font;
                font.setBold(true);
                return font;
            }
            return QVariant();

        case Qt::BackgroundRole:
            if (item->channelId() == current_channel_id_) {
                return QColor(42, 87, 141, 80);  // Subtle blue highlight
            }
            return QVariant();

        case ChannelIdRole:
            return QVariant::fromValue(item->channelId().value);

        case IsCurrentChannelRole:
            return item->channelId() == current_channel_id_;

        case UserCountRole: {
            auto it = user_counts_.find(item->channelId());
            return it != user_counts_.end() ? it->second : 0;
        }
    }
}
```

**setCurrentChannel implementation**:
```cpp
void ChannelTreeModel::setCurrentChannel(ChannelId channel_id)
{
    ChannelId old_channel = current_channel_id_;
    current_channel_id_ = channel_id;

    // Emit dataChanged for old and new channel items
    if (old_channel) {
        QModelIndex old_idx = indexFromChannelId(old_channel);
        if (old_idx.isValid()) emit dataChanged(old_idx, old_idx);
    }
    QModelIndex new_idx = indexFromChannelId(channel_id);
    if (new_idx.isValid()) emit dataChanged(new_idx, new_idx);
}
```

### 4.2 ChannelItemDelegate (New)

A custom `QStyledItemDelegate` that provides richer rendering:
- Current channel: bold text, subtle background highlight, different icon tint
- User count badge: small circular badge with user count to the right of the channel name
- Joined indicator: small green dot or checkmark next to the current channel

```cpp
class ChannelItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ChannelItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};
```

The `paint()` method will:
1. Call base `QStyledItemDelegate::paint()` for standard rendering
2. If `IsCurrentChannelRole` is true, draw a subtle left-side accent bar (4px wide, accent color)
3. Draw user count badge (small rounded rect with number) if `UserCountRole > 0`
4. Draw a small green dot indicator for the current channel

### 4.3 MainWindow Enhancements

**New slots**:
```cpp
private slots:
    void onLeaveChannelRequested();
```

**New private methods**:
```cpp
    void loadMockChannelData();  // For standalone mode
```

**Duplicate join guard** in `onJoinChannelRequested`:
```cpp
void MainWindow::onJoinChannelRequested(ChannelId channel_id)
{
#ifdef NEVO_HAS_BOOST
    // Guard: already in this channel
    if (client_core_->isInChannel()) {
        auto snapshot = client_core_->getState();
        if (snapshot.current_channel == channel_id) {
            statusBar()->showMessage(
                QStringLiteral("Already in this channel."), 3000);
            return;
        }
    }

    if (!client_core_->isConnected()) {
        QMessageBox::warning(this, ...);
        return;
    }
    // ... existing join logic ...

    // Add success feedback after coroutine
    // On success: statusBar()->showMessage("Joined channel: %1")
#endif
}
```

**Leave channel slot**:
```cpp
void MainWindow::onLeaveChannelRequested()
{
#ifdef NEVO_HAS_BOOST
    if (!client_core_->isInChannel()) {
        statusBar()->showMessage(QStringLiteral("Not in a channel."), 3000);
        return;
    }

    boost::asio::co_spawn(*io_ctx_,
        [this]() -> boost::asio::awaitable<void> {
            auto result = co_await client_core_->leaveChannel();
            if (!result) {
                QPointer<MainWindow> guard(this);
                QMetaObject::invokeMethod(this, [guard, result]() {
                    if (!guard) return;
                    QMessageBox::warning(guard,
                        QStringLiteral("Leave Channel Error"),
                        QString("Failed to leave channel: %1")
                            .arg(QString::fromStdString(result.error().message())));
                }, Qt::QueuedConnection);
            }
        },
        boost::asio::detached);
#endif
}
```

**Context menu update** (`onChannelContextMenu`):
```cpp
void MainWindow::onChannelContextMenu(const QPoint& pos)
{
    QModelIndex index = channel_tree_->indexAt(pos);
    if (!index.isValid()) return;

    QMenu context_menu(this);
    ChannelId channel_id = channel_model_->channelIdFromIndex(index);

    bool is_current = index.data(IsCurrentChannelRole).toBool();

    if (is_current) {
        QAction* leave_action = context_menu.addAction(
            QStringLiteral("Leave Channel"));
        if (context_menu.exec(...) == leave_action) {
            onLeaveChannelRequested();
        }
    } else {
        QAction* join_action = context_menu.addAction(
            QStringLiteral("Join Channel"));
        if (context_menu.exec(...) == join_action) {
            onJoinChannelRequested(channel_id);
        }
    }
}
```

**Wire onStateChanged to update current channel in model**:
```cpp
// In setupClientCoreCallbacks():
client_core_->onStateChanged =
    [this](ClientState new_state, ClientState old_state) {
        // ... existing code ...
        if (new_state == ClientState::InChannel) {
            auto snapshot = client_core_->getState();
            channel_model_->setCurrentChannel(snapshot.current_channel);
            statusBar()->showMessage(
                QString("Joined channel: %1")
                    .arg(QString::fromStdString(snapshot.current_channel_name)));
        } else if (old_state == ClientState::InChannel) {
            channel_model_->clearCurrentChannel();
        }
    };
```

**Mock data for standalone mode**:
```cpp
void MainWindow::loadMockChannelData()
{
    std::vector<ChannelInfo> mock_channels = {
        ChannelInfo(ChannelId(1), "Root", ChannelId(0)),
        ChannelInfo(ChannelId(2), "General", ChannelId(1)),
        ChannelInfo(ChannelId(3), "Chat", ChannelId(2)),
        ChannelInfo(ChannelId(4), "Gaming", ChannelId(2)),
        ChannelInfo(ChannelId(5), "Music", ChannelId(1)),
        ChannelInfo(ChannelId(6), "AFK", ChannelId(1)),
        ChannelInfo(ChannelId(7), "Development", ChannelId(1)),
        ChannelInfo(ChannelId(8), "Frontend", ChannelId(7)),
        ChannelInfo(ChannelId(9), "Backend", ChannelId(7)),
    };

    channel_model_->updateFromChannelList(mock_channels);
    channel_tree_->expandAll();

    // Simulate user counts
    channel_model_->setChannelUserCount(ChannelId(3), 5);
    channel_model_->setChannelUserCount(ChannelId(4), 12);
    channel_model_->setChannelUserCount(ChannelId(5), 2);

    statusBar()->showMessage(
        QStringLiteral("Demo mode - Mock channel data loaded"), 5000);
}
```

### 4.4 IconProvider Enhancement

Add a "current channel" icon variant with a different color/glow:
```cpp
static QIcon currentChannelIcon(int size = 20);
```
This icon uses the same folder shape but with a green accent to indicate active channel.

### 4.5 CMakeLists.txt Update

Add new files:
```cmake
add_executable(nevo_client_ui
    ...
    src/ChannelItemDelegate.cpp
    include/nevo/ui/ChannelItemDelegate.h
    ...
)
```

## 5. Boundary Conditions & Exception Handling

| Scenario | Handling |
|----------|----------|
| Join same channel again | Guard check, show status message "Already in this channel" |
| Join channel while not connected | Warning dialog "Please connect to a server first" |
| Network error during join | Catch in coroutine, show QMessageBox with error |
| Leave channel while not in one | Status message "Not in a channel" |
| Server disconnects while in channel | `handleDisconnected()` clears state, `clearCurrentChannel()` called |
| Empty channel list from server | Tree shows empty state, no crash |
| Mock data in standalone mode | Loaded after `setupUi()`, tree expandAll() called |
| Channel list update while in channel | `current_channel_id_` preserved across `updateFromChannelList()` calls |

## 6. Data Flow Paths

### Join Channel Flow
```
User double-clicks channel item
  -> ChannelTreeModel::onDoubleClicked(index)
  -> emit joinChannelRequested(channel_id)
  -> MainWindow::onJoinChannelRequested(channel_id)
     -> Guard: already in this channel? -> show status, return
     -> Guard: not connected? -> warning dialog, return
     -> co_await client_core_->joinChannel(channel_id)
        -> On success: state changes to InChannel
           -> onStateChanged callback
              -> channel_model_->setCurrentChannel(channel_id)
              -> statusBar shows "Joined channel: ..."
        -> On failure: QMessageBox with error
```

### Leave Channel Flow
```
User right-clicks current channel -> "Leave Channel"
  -> MainWindow::onLeaveChannelRequested()
     -> Guard: not in channel? -> status message, return
     -> co_await client_core_->leaveChannel()
        -> On success: state changes to Connected
           -> onStateChanged callback
              -> channel_model_->clearCurrentChannel()
              -> statusBar shows "Left channel"
        -> On failure: QMessageBox with error
```

### Channel List Update Flow
```
Server sends ChannelList
  -> ClientCore::handleChannelEvent()
  -> onChannelList callback
  -> MainWindow (via postToUiThread)
  -> channel_model_->updateFromChannelList(channels)
     -> Rebuilds tree, preserves current_channel_id_ highlighting
     -> Emits dataChanged for current channel item
```

## 7. Expected Outcomes

1. Channel tree visually distinguishes the currently joined channel (bold, accent highlight, different icon)
2. User counts are displayed next to channel names in parentheses
3. Right-click context menu shows "Leave Channel" for the current channel, "Join Channel" for others
4. Join/leave operations provide success/failure feedback via status bar or dialog
5. Duplicate join attempts are gracefully handled
6. Standalone mode loads mock channel data for UI demonstration
7. Custom delegate renders channel items with accent bar and user count badge
