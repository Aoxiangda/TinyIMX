#include "common/observability/Trace.h"

#include <array>
#include <chrono>
#include <utility>

#include <opentelemetry/context/context.h>
#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/context/propagation/text_map_propagator.h>
#include <opentelemetry/nostd/span.h>
#include <opentelemetry/trace/context.h>
#include <opentelemetry/trace/propagation/http_trace_context.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/scope.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_startoptions.h>
#include <opentelemetry/trace/tracer.h>

namespace tinyimx::observability {
namespace {

namespace otel_context = opentelemetry::context;
namespace otel_propagation = opentelemetry::context::propagation;
namespace otel_trace = opentelemetry::trace;

class HeaderCarrier final : public otel_propagation::TextMapCarrier {
public:
    explicit HeaderCarrier(TraceHeaders* headers) : mutable_headers_(headers), headers_(headers) {}
    explicit HeaderCarrier(const TraceHeaders* headers) : headers_(headers) {}

    opentelemetry::nostd::string_view Get(
        opentelemetry::nostd::string_view key
    ) const noexcept override {
        if (headers_ == nullptr) {
            return {};
        }
        const auto it = headers_->find(std::string(key.data(), key.size()));
        if (it == headers_->end()) {
            return {};
        }
        return opentelemetry::nostd::string_view(it->second.data(), it->second.size());
    }

    void Set(
        opentelemetry::nostd::string_view key,
        opentelemetry::nostd::string_view value
    ) noexcept override {
        if (mutable_headers_ == nullptr) {
            return;
        }
        (*mutable_headers_)[std::string(key.data(), key.size())] =
            std::string(value.data(), value.size());
    }

private:
    TraceHeaders* mutable_headers_{nullptr};
    const TraceHeaders* headers_{nullptr};
};

otel_trace::SpanKind ToOtelKind(SpanKind kind) {
    switch (kind) {
        case SpanKind::kClient:
            return otel_trace::SpanKind::kClient;
        case SpanKind::kServer:
            return otel_trace::SpanKind::kServer;
        case SpanKind::kInternal:
        default:
            return otel_trace::SpanKind::kInternal;
    }
}

TraceIds SpanIds(const opentelemetry::nostd::shared_ptr<otel_trace::Span>& span) noexcept {
    TraceIds out;
    if (!span) {
        return out;
    }
    const auto context = span->GetContext();
    if (!context.IsValid()) {
        return out;
    }

    std::array<char, otel_trace::TraceId::kSize * 2> trace_hex{};
    std::array<char, otel_trace::SpanId::kSize * 2> span_hex{};
    context.trace_id().ToLowerBase16(
        opentelemetry::nostd::span<char, otel_trace::TraceId::kSize * 2>{
            trace_hex.data(), trace_hex.size()
        }
    );
    context.span_id().ToLowerBase16(
        opentelemetry::nostd::span<char, otel_trace::SpanId::kSize * 2>{
            span_hex.data(), span_hex.size()
        }
    );
    out.trace_id.assign(trace_hex.data(), trace_hex.size());
    out.span_id.assign(span_hex.data(), span_hex.size());
    return out;
}

std::unique_ptr<TraceSpan::Impl> StartImpl(
    const std::string& name,
    SpanKind kind,
    const otel_context::Context* parent
);

}  // namespace

struct TraceSpan::Impl {
    opentelemetry::nostd::shared_ptr<otel_trace::Span> span;
    std::unique_ptr<otel_trace::Scope> scope;
    std::chrono::steady_clock::time_point started_at{std::chrono::steady_clock::now()};
    bool ended{false};
    bool status_set{false};
    std::string default_error_type;
};

namespace {

std::unique_ptr<TraceSpan::Impl> StartImpl(
    const std::string& name,
    SpanKind kind,
    const otel_context::Context* parent
) {
    auto provider = otel_trace::Provider::GetTracerProvider();
    if (!provider) {
        return {};
    }
    auto tracer = provider->GetTracer("tinyimx.observability", "m20");
    if (!tracer) {
        return {};
    }

    otel_trace::StartSpanOptions options;
    options.kind = ToOtelKind(kind);
    if (parent != nullptr) {
        options.parent = *parent;
    }

    auto span = tracer->StartSpan(
        opentelemetry::nostd::string_view(name.data(), name.size()),
        options
    );
    if (!span) {
        return {};
    }

    auto impl = std::make_unique<TraceSpan::Impl>();
    impl->span = span;
    impl->started_at = std::chrono::steady_clock::now();
    impl->scope = std::make_unique<otel_trace::Scope>(span);
    return impl;
}

}  // namespace

TraceSpan::TraceSpan() noexcept = default;
TraceSpan::TraceSpan(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
TraceSpan::~TraceSpan() { End(); }
TraceSpan::TraceSpan(TraceSpan&& other) noexcept = default;
TraceSpan& TraceSpan::operator=(TraceSpan&& other) noexcept {
    if (this != &other) {
        End();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool TraceSpan::Valid() const noexcept {
    return impl_ && impl_->span && impl_->span->GetContext().IsValid();
}

void TraceSpan::SetAttribute(const std::string& key, const std::string& value) noexcept {
    if (impl_ && impl_->span) {
        impl_->span->SetAttribute(
            opentelemetry::nostd::string_view(key.data(), key.size()),
            opentelemetry::nostd::string_view(value.data(), value.size())
        );
    }
}
void TraceSpan::SetAttribute(const std::string& key, const char* value) noexcept {
    SetAttribute(key, value == nullptr ? std::string{} : std::string(value));
}
void TraceSpan::SetAttribute(const std::string& key, std::int64_t value) noexcept {
    if (impl_ && impl_->span) {
        impl_->span->SetAttribute(
            opentelemetry::nostd::string_view(key.data(), key.size()),
            value
        );
    }
}
void TraceSpan::SetAttribute(const std::string& key, bool value) noexcept {
    if (impl_ && impl_->span) {
        impl_->span->SetAttribute(
            opentelemetry::nostd::string_view(key.data(), key.size()),
            value
        );
    }
}
void TraceSpan::SetAttribute(const std::string& key, double value) noexcept {
    if (impl_ && impl_->span) {
        impl_->span->SetAttribute(
            opentelemetry::nostd::string_view(key.data(), key.size()),
            value
        );
    }
}

void TraceSpan::MarkOk() noexcept {
    if (impl_ && impl_->span) {
        impl_->span->SetStatus(otel_trace::StatusCode::kOk);
        impl_->status_set = true;
    }
}

void TraceSpan::MarkError(
    const std::string& error_type,
    const std::string& description
) noexcept {
    if (!impl_ || !impl_->span) return;
    if (!error_type.empty()) {
        impl_->span->SetAttribute(
            "error.type",
            opentelemetry::nostd::string_view(error_type.data(), error_type.size())
        );
    }
    impl_->span->SetStatus(
        otel_trace::StatusCode::kError,
        opentelemetry::nostd::string_view(description.data(), description.size())
    );
    impl_->status_set = true;
}

void TraceSpan::SetDefaultErrorOnEnd(const std::string& error_type) noexcept {
    if (impl_) {
        impl_->default_error_type = error_type;
    }
}

void TraceSpan::AddEvent(const std::string& name) noexcept {
    if (impl_ && impl_->span && !name.empty()) {
        impl_->span->AddEvent(
            opentelemetry::nostd::string_view(name.data(), name.size())
        );
    }
}

void TraceSpan::End() noexcept {
    if (!impl_ || impl_->ended) {
        return;
    }
    if (!impl_->status_set && !impl_->default_error_type.empty()) {
        MarkError(impl_->default_error_type);
    }
    impl_->ended = true;
    if (impl_->span) {
        impl_->span->End();
    }
    impl_->scope.reset();
}

TraceIds TraceSpan::Ids() const noexcept {
    return impl_ ? SpanIds(impl_->span) : TraceIds{};
}

double TraceSpan::ElapsedSeconds() const noexcept {
    if (!impl_) return 0.0;
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - impl_->started_at
    ).count();
}

TraceSpan StartSpan(const std::string& name, SpanKind kind) {
    return TraceSpan(StartImpl(name, kind, nullptr));
}

TraceSpan StartServerSpanFromHeaders(
    const std::string& name,
    const TraceHeaders& headers
) {
    HeaderCarrier carrier(&headers);
    otel_context::Context base;
    otel_trace::propagation::HttpTraceContext propagator;
    auto extracted = propagator.Extract(carrier, base);
    if (!otel_trace::GetSpan(extracted)->GetContext().IsValid()) {
        extracted = extracted.SetValue(otel_trace::kIsRootSpanKey, true);
    }
    return TraceSpan(StartImpl(name, SpanKind::kServer, &extracted));
}

void InjectCurrentTraceHeaders(TraceHeaders* headers) noexcept {
    if (headers == nullptr) {
        return;
    }
    HeaderCarrier carrier(headers);
    otel_trace::propagation::HttpTraceContext propagator;
    auto current = otel_context::RuntimeContext::GetCurrent();
    propagator.Inject(carrier, current);
}

TraceIds CurrentTraceIds() noexcept {
    return SpanIds(otel_trace::Tracer::GetCurrentSpan());
}

}  // namespace tinyimx::observability
