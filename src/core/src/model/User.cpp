/**
 * @file User.cpp
 * @brief 用户模型实现
 */

#include "nevo/core/model/User.h"

namespace nevo {

User::User(UserId id, const std::string& username, GroupId group_id)
    : id_(id)
    , username_(username)
    , status_(UserStatus::Online)
    , group_id_(group_id)
{
}

} // namespace nevo
