#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx::rpc {

struct ListFriendsRpcRequest {
    std::uint64_t actor_user_id{0};
    std::uint32_t limit{0};
};

struct SocialFriend {
    std::uint64_t friend_user_id{0};
    std::string username;
    std::string nickname;
    std::string avatar_url;
    std::uint32_t user_status{0};
    std::uint32_t relation_status{0};
    std::string relation_created_at;
    std::string relation_updated_at;
};

struct ListFriendsRpcResponse {
    std::vector<SocialFriend> friends;
    bool has_more{false};
};

}  // namespace tinyimx::rpc
