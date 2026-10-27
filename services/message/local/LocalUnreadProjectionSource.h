#pragma once
#include "services/message/local/LocalPrivateStore.h"
#include "services/projection/unread/UnreadProjectionReader.h"
#include <limits>
#include <stdexcept>
namespace tinyimx::message::local {
class LocalUnreadProjectionSource final : public projection::unread::UnreadProjectionSource {
public:
    explicit LocalUnreadProjectionSource(LocalPrivateStore* store):store_(store){if(!store_)throw std::invalid_argument("local unread requires owner");}
    projection::unread::UnreadProjectionReadResult<projection::unread::DialogUnreadSnapshot>
    LoadDialogSnapshot(std::uint64_t receiver,std::uint64_t peer)override{
        projection::unread::UnreadProjectionReadResult<projection::unread::DialogUnreadSnapshot> out;
        const auto read=store_->ReadUnreadSnapshot(receiver,peer);out.message=read.message;
        if(read.Succeeded()&&read.private_unread<=read.total_unread&&read.total_unread<=std::numeric_limits<std::int64_t>::max()){
            out.success=true;out.value={receiver,peer,static_cast<std::int64_t>(read.private_unread),static_cast<std::int64_t>(read.total_unread)};
        }
        return out;
    }
    projection::unread::UnreadProjectionReadResult<projection::unread::UserUnreadSnapshot>
    LoadUserSnapshot(std::uint64_t receiver)override{
        projection::unread::UnreadProjectionReadResult<projection::unread::UserUnreadSnapshot> out;
        auto read=store_->ReadUserUnreadSnapshot(receiver);out.message=read.message;
        if(!read.Succeeded()||read.total_unread>std::numeric_limits<std::int64_t>::max())return out;
        out.value.receiver_user_id=receiver;out.value.total_unread=static_cast<std::int64_t>(read.total_unread);
        for(const auto&[peer,count]:read.peer_counts)out.value.peer_counts.emplace_back(peer,static_cast<std::int64_t>(count));
        out.success=true;return out;
    }
private:
    LocalPrivateStore* store_;
};
}
