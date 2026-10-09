#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace tinyimx::observability {

enum class SpanKind {
    kInternal = 0,
    kClient,
    kServer
};

struct TraceIds {
    std::string trace_id;
    std::string span_id;

    bool Valid() const noexcept {
        return !trace_id.empty() && !span_id.empty();
    }
};

using TraceHeaders = std::unordered_map<std::string, std::string>;

class TraceSpan final {
public:
    struct Impl;

    TraceSpan() noexcept;
    ~TraceSpan();

    TraceSpan(TraceSpan&& other) noexcept;
    TraceSpan& operator=(TraceSpan&& other) noexcept;

    TraceSpan(const TraceSpan&) = delete;
    TraceSpan& operator=(const TraceSpan&) = delete;

    bool Valid() const noexcept;
    void SetAttribute(const std::string& key, const std::string& value) noexcept;
    void SetAttribute(const std::string& key, const char* value) noexcept;
    void SetAttribute(const std::string& key, std::int64_t value) noexcept;
    void SetAttribute(const std::string& key, bool value) noexcept;
    void SetAttribute(const std::string& key, double value) noexcept;
    void MarkOk() noexcept;
    void MarkError(const std::string& error_type,
                   const std::string& description = {}) noexcept;
    void SetDefaultErrorOnEnd(const std::string& error_type) noexcept;
    void AddEvent(const std::string& name) noexcept;
    void End() noexcept;
    TraceIds Ids() const noexcept;
    double ElapsedSeconds() const noexcept;

private:
    explicit TraceSpan(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;

    friend TraceSpan StartSpan(const std::string&, SpanKind);
    friend TraceSpan StartServerSpanFromHeaders(
        const std::string&, const TraceHeaders&);
};

TraceSpan StartSpan(const std::string& name, SpanKind kind = SpanKind::kInternal);
TraceSpan StartServerSpanFromHeaders(
    const std::string& name,
    const TraceHeaders& headers
);

void InjectCurrentTraceHeaders(TraceHeaders* headers) noexcept;
TraceIds CurrentTraceIds() noexcept;

}  // namespace tinyimx::observability
