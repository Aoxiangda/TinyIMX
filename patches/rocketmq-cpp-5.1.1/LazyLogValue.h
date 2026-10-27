#pragma once
// TinyIMX opt-in SDK candidate. C++11 matches the upstream SDK build.
#include "rocketmq/RocketMQ.h"
#include "spdlog/fmt/fmt.h"
#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>

ROCKETMQ_NAMESPACE_BEGIN
namespace logging {
inline bool LazyDebugFormattingEnabled() {
    static const bool enabled=[] {
        const char* value=std::getenv("TINYIMX_ROCKETMQ_LAZY_DEBUG_ENABLE");
        return value&&value[0]=='1'&&value[1]=='\0';
    }();
    return enabled;
}

template <typename Producer>
class LazyLogValue {
public:
    explicit LazyLogValue(Producer producer)
        : producer_(std::move(producer)),lazy_(LazyDebugFormattingEnabled()) {
        // OFF keeps evaluation at the original argument-construction boundary.
        if(!lazy_)eager_=producer_();
    }
    bool IsLazy()const{return lazy_;}
    std::string Produce()const{return producer_();}
    const std::string& EagerValue()const{return eager_;}
private:
    Producer producer_;
    bool lazy_;
    std::string eager_;
};

template <typename Producer>
LazyLogValue<typename std::decay<Producer>::type> MakeLazyLogValue(Producer&& producer) {
    return LazyLogValue<typename std::decay<Producer>::type>(std::forward<Producer>(producer));
}
} // namespace logging
ROCKETMQ_NAMESPACE_END

FMT_BEGIN_NAMESPACE
template <typename Producer>
struct formatter<ROCKETMQ_NAMESPACE::logging::LazyLogValue<Producer>>:formatter<string_view> {
    template <typename Context>
    auto format(const ROCKETMQ_NAMESPACE::logging::LazyLogValue<Producer>& value,Context& context)const
        ->decltype(context.out()) {
        if(value.IsLazy()) {
            // fmt consumes this view before the owning string leaves scope.
            const std::string text=value.Produce();
            return formatter<string_view>::format(string_view(text.data(),text.size()),context);
        }
        const std::string& text=value.EagerValue();
        return formatter<string_view>::format(string_view(text.data(),text.size()),context);
    }
};
FMT_END_NAMESPACE
