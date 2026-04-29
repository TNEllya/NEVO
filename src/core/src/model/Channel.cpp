/**
 * @file Channel.cpp
 * @brief 树状频道模型实现
 */

#include "nevo/core/model/Channel.h"
#include <algorithm>

namespace nevo {

Channel::Channel(ChannelId id, const std::string& name, Channel* parent)
    : id_(id)
    , name_(name)
    , parent_(parent)
{
}

void Channel::addChild(Channel* child) {
    if (child && child != this) {
        children_.push_back(child);
        child->parent_ = this;
    }
}

void Channel::removeChild(Channel* child) {
    auto it = std::find(children_.begin(), children_.end(), child);
    if (it != children_.end()) {
        children_.erase(it);
        if (child->parent_ == this) {
            child->parent_ = nullptr;
        }
    }
}

Channel* Channel::findChild(ChannelId cid) const {
    // 广度优先搜索直接子频道
    for (auto* child : children_) {
        if (child->id() == cid) {
            return child;
        }
    }
    // 递归搜索
    for (auto* child : children_) {
        auto* found = child->findChild(cid);
        if (found) {
            return found;
        }
    }
    return nullptr;
}

void Channel::addUser(UserId uid) {
    if (!hasUser(uid)) {
        users_.push_back(uid);
    }
}

void Channel::removeUser(UserId uid) {
    auto it = std::find(users_.begin(), users_.end(), uid);
    if (it != users_.end()) {
        users_.erase(it);
    }
}

bool Channel::hasUser(UserId uid) const {
    return std::find(users_.begin(), users_.end(), uid) != users_.end();
}

} // namespace nevo
