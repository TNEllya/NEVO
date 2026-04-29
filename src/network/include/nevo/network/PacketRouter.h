#pragma once
/**
 * @file PacketRouter.h
 * @brief 消息路由器
 *
 * 根据消息类型将收到的消息路由到对应的处理函数。
 * 用于解耦消息接收和消息处理逻辑：
 *   - 网络层只负责接收消息并调用 router.route()
 *   - 业务层注册各自感兴趣的消息类型处理器
 *
 * 使用方式：
 *   PacketRouter router;
 *   router.registerHandler(static_cast<uint32_t>(ControlMessageType::LoginRequest),
 *       [](const std::vector<uint8_t>& data) {
 *           // 处理登录请求
 *       });
 *   router.registerHandler(static_cast<uint32_t>(ControlMessageType::JoinChannel),
 *       [](const std::vector<uint8_t>& data) {
 *           // 处理加入频道
 *       });
 *
 *   // 收到消息时：
 *   router.route(message_type, payload_data);
 */

#include <functional>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <mutex>
#include <optional>

#include "nevo/core/common/Logger.h"
#include "nevo/core/protocol/PacketTypes.h"

namespace nevo {

// ============================================================
// 消息路由器类
// ============================================================

class PacketRouter {
public:
    /// 消息处理回调类型
    /// @param data 消息载荷数据
    using HandlerCallback = std::function<void(const std::vector<uint8_t>& data)>;

    /// 默认构造函数
    PacketRouter();

    /// 析构函数
    ~PacketRouter();

    // 禁止拷贝
    PacketRouter(const PacketRouter&) = delete;
    PacketRouter& operator=(const PacketRouter&) = delete;

    // ============================================================
    // 处理器注册
    // ============================================================

    /// 注册消息处理器
    /// 如果该类型已有处理器，将替换旧的。
    ///
    /// @param message_type 消息类型（ControlMessageType 枚举值转为 uint32_t）
    /// @param callback 处理回调
    void registerHandler(uint32_t message_type, HandlerCallback callback);

    /// 注销消息处理器
    /// @param message_type 消息类型
    /// @return true 成功注销
    bool unregisterHandler(uint32_t message_type);

    /// 注册默认处理器
    /// 当收到未注册类型的消息时调用。
    /// @param callback 默认处理回调
    void setDefaultHandler(HandlerCallback callback);

    // ============================================================
    // 消息路由
    // ============================================================

    /// 路由消息到对应的处理器
    /// 根据消息类型查找已注册的处理器并调用。
    /// 如果没有找到对应处理器，调用默认处理器（如果已设置）。
    ///
    /// @param message_type 消息类型
    /// @param data 消息载荷数据
    void route(uint32_t message_type, const std::vector<uint8_t>& data);

    // ============================================================
    // 查询
    // ============================================================

    /// 检查指定消息类型是否已注册处理器
    /// @param message_type 消息类型
    /// @return true 已注册
    bool hasHandler(uint32_t message_type) const;

    /// 获取已注册的消息类型列表
    /// @return 消息类型列表
    std::vector<uint32_t> registeredTypes() const;

private:
    // ============================================================
    // 成员变量
    // ============================================================

    /// 消息类型 -> 处理回调 映射
    std::unordered_map<uint32_t, HandlerCallback> handlers_;

    /// 默认处理器
    HandlerCallback default_handler_;

    /// 互斥锁，保护 handlers_
    mutable std::mutex mutex_;
};

} // namespace nevo
