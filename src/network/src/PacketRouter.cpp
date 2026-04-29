/**
 * @file PacketRouter.cpp
 * @brief 消息路由器实现
 *
 * 基于消息类型的简单路由实现。使用 unordered_map 存储消息类型
 * 到处理回调的映射，查找时间复杂度为 O(1)。
 *
 * 设计说明：
 *   - route() 方法在锁外调用回调，避免回调中注册/注销处理器导致死锁
 *   - 同一消息类型只能有一个处理器，后注册的会覆盖先注册的
 *   - 默认处理器用于处理未注册类型的消息（如日志、协议升级协商等）
 */

#include "nevo/network/PacketRouter.h"

namespace nevo {

// ============================================================
// 构造 / 析构
// ============================================================

PacketRouter::PacketRouter()
{
    NEVO_LOG_DEBUG("network", "PacketRouter constructed");
}

PacketRouter::~PacketRouter()
{
    NEVO_LOG_DEBUG("network", "PacketRouter destroyed");
}

// ============================================================
// registerHandler - 注册处理器
// ============================================================

void PacketRouter::registerHandler(uint32_t message_type, HandlerCallback callback)
{
    if (!callback) {
        NEVO_LOG_WARN("network",
                       "Attempted to register null handler for message type {}",
                       message_type);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 检查是否已有处理器
    auto it = handlers_.find(message_type);
    if (it != handlers_.end()) {
        NEVO_LOG_WARN("network",
                       "Replacing existing handler for message type {}",
                       message_type);
    }

    handlers_[message_type] = std::move(callback);

    NEVO_LOG_INFO("network",
                  "Handler registered for message type {} (total: {})",
                  message_type, handlers_.size());
}

// ============================================================
// unregisterHandler - 注销处理器
// ============================================================

bool PacketRouter::unregisterHandler(uint32_t message_type)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = handlers_.find(message_type);
    if (it == handlers_.end()) {
        NEVO_LOG_DEBUG("network",
                       "No handler to unregister for message type {}",
                       message_type);
        return false;
    }

    handlers_.erase(it);

    NEVO_LOG_INFO("network",
                  "Handler unregistered for message type {} (remaining: {})",
                  message_type, handlers_.size());
    return true;
}

// ============================================================
// setDefaultHandler - 设置默认处理器
// ============================================================

void PacketRouter::setDefaultHandler(HandlerCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    default_handler_ = std::move(callback);

    NEVO_LOG_INFO("network", "Default handler set");
}

// ============================================================
// route - 路由消息
// ============================================================

void PacketRouter::route(uint32_t message_type, const std::vector<uint8_t>& data)
{
    // 在锁内查找处理器，在锁外调用回调
    HandlerCallback handler;
    bool use_default = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = handlers_.find(message_type);
        if (it != handlers_.end()) {
            handler = it->second;
        } else if (default_handler_) {
            handler = default_handler_;
            use_default = true;
        }
    }

    if (handler) {
        NEVO_LOG_TRACE("network",
                       "Routing message type {} ({} bytes) to {} handler",
                       message_type, data.size(),
                       use_default ? "default" : "registered");

        try {
            handler(data);
        } catch (const std::exception& e) {
            NEVO_LOG_ERROR("network",
                           "Exception in handler for message type {}: {}",
                           message_type, e.what());
        } catch (...) {
            NEVO_LOG_ERROR("network",
                           "Unknown exception in handler for message type {}",
                           message_type);
        }
    } else {
        NEVO_LOG_WARN("network",
                       "No handler for message type {}, discarding {} bytes",
                       message_type, data.size());
    }
}

// ============================================================
// hasHandler - 检查处理器是否存在
// ============================================================

bool PacketRouter::hasHandler(uint32_t message_type) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return handlers_.find(message_type) != handlers_.end();
}

// ============================================================
// registeredTypes - 获取已注册的消息类型列表
// ============================================================

std::vector<uint32_t> PacketRouter::registeredTypes() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint32_t> types;
    types.reserve(handlers_.size());

    for (const auto& [type, _] : handlers_) {
        types.push_back(type);
    }

    return types;
}

} // namespace nevo
