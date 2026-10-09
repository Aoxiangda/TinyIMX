#include "common/security/AuthPhaseTrace.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
using Trace = tinyimx::diagnostics::AuthPhaseTrace;
namespace {
std::int64_t wall = 0, cpu = 0;
unsigned clocks = 0, cpu_clocks = 0, emitted = 0, failed = 0;
Trace::Snapshot captured;
std::int64_t Wall() noexcept { ++clocks; return wall; }
std::int64_t Cpu() noexcept { ++cpu_clocks; return cpu; }
std::int64_t NoCpu() noexcept { return -1; }
void Sink(const Trace::Snapshot& s) noexcept { captured = s; ++emitted; }
void Check(bool ok, const char* label) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << label << '\n'; failed += !ok;
}
}
int main() {
    static_assert(std::is_trivially_copyable_v<Trace::Snapshot>);
    Check(!tinyimx::diagnostics::AuthPhaseEnabled(), "Authentication diagnostic defaults OFF");
    {
        Trace t(1, false, Sink, Wall, Cpu);
        Check(t.Measure(Trace::Phase::Password, [] { return true; }), "Disabled phase preserves result");
        t.Result(0, 16);
    }
    Check(clocks == 0 && cpu_clocks == 0 && emitted == 0, "Disabled trace reads no clocks or sink");
    {
        Trace t(1, true, Sink, Wall, Cpu);
        auto value = t.Measure(Trace::Phase::Lookup, [] {
            wall += 20; cpu += 2; return std::make_unique<int>(7);
        });
        Check(value && *value == 7, "Move-only lookup ownership preserved");
        t.Measure(Trace::Phase::Password, [] { wall += 80; cpu += 60; });
        t.Measure(Trace::Phase::Password, [] { wall += 10; cpu += 5; });
        t.Result(0, 16);
    }
    Check(emitted == 1 && captured.uid == 16 && captured.kind == 1 && captured.status == 0,
          "Final numeric identity and repository status captured");
    Check(captured.total_us == 110 && captured.cpu_us == 67, "Wall and thread CPU separated");
    Check(captured.phase_us[0] == 20 && captured.phase_cpu_us[0] == 2 &&
          captured.phase_us[1] == 90 && captured.phase_cpu_us[1] == 65,
          "Repeated phase timing accumulates accurately");
    { Trace t(2, true, Sink, Wall, Cpu); t.Result(0, 17); }
    Check(emitted == 1, "Unsampled successful identity omitted");
    { Trace t(2, true, Sink, Wall, Cpu); t.Result(0, 0); }
    Check(emitted == 1, "Zero identity is not a successful sample");
    { Trace t(1, true, Sink, Wall, Cpu); t.Result(5, 17); }
    Check(emitted == 2 && captured.status == 5, "Business authentication failure remains eligible");
    { Trace t(2, true, Sink, Wall, Cpu); t.Result(0, 0, 4); }
    Check(emitted == 3 && captured.outcome == 4, "Handler business failure not confused with gRPC OK");
    { Trace t(2, true, Sink, Wall, NoCpu); wall += 5; t.Result(0, 32); }
    Check(emitted == 4 && captured.cpu_us == -1 && captured.phase_us[0] == -1,
          "Missing CPU and unexecuted phases remain explicit");
    try {
        Trace t(1, true, Sink, Wall, Cpu);
        t.Measure(Trace::Phase::Lookup, [] { wall += 13; cpu += 3; throw std::runtime_error("original"); });
    } catch (const std::runtime_error& e) {
        Check(std::string(e.what()) == "original", "Original exception propagates unchanged");
    }
    Check(emitted == 5 && captured.threw && captured.phase_us[0] == 13 &&
          captured.phase_cpu_us[0] == 3 && captured.status == -1,
          "Throwing phase retains timing and unfinished result");
    tinyimx::diagnostics::AuthPhaseLogLimiter limiter;
    unsigned accepted = 0; for (unsigned i = 0; i < 100; ++i) accepted += limiter.Admit(10);
    Check(accepted == 8, "Combined repository and handler logs bounded to eight per bucket");
    Check(limiter.Admit(11) && !limiter.Admit(10), "Next bucket resumes and stale bucket rejected");
    return failed ? 1 : 0;
}
