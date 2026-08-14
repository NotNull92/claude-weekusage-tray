// The entire data model of this app.
//
// Four scalars, nothing else. There is no field here for a token, an account
// id, a session id, or a transcript, and there is deliberately nowhere to put
// one.
#pragma once

#include <string>

namespace cwut {

// One rate-limit window as reported by Claude Code's status-line payload.
struct RateWindow {
    bool hasUsage = false;
    // Percentage of the window already consumed, clamped to 0-100. Fractional
    // input is rounded up so the remaining figure never overstates headroom.
    int usedPercent = 0;
    bool hasReset = false;
    long long resetsAt = 0;  // Unix seconds, only set after validation.

    // Remaining headroom in percent. Meaningless unless hasUsage is true.
    int remainingPercent() const { return 100 - usedPercent; }
};

struct UsageSnapshot {
    RateWindow fiveHour;
    RateWindow sevenDay;
    // Unix seconds when this process received the payload. 0 means "nothing
    // has ever arrived", which is a different state from "arrived but empty".
    long long receivedAtUnix = 0;

    bool hasAnyUsage() const { return fiveHour.hasUsage || sevenDay.hasUsage; }
};

// Timestamps outside this range are rejected instead of being rendered.
constexpr long long kMinEpochSeconds = 1577836800LL;  // 2020-01-01T00:00:00Z
constexpr long long kMaxEpochSeconds = 4102444800LL;  // 2100-01-01T00:00:00Z

// A snapshot older than this is shown, but marked as stale.
constexpr long long kStaleAfterSeconds = 15 * 60;

// Reads rate_limits.five_hour / rate_limits.seven_day out of a status-line
// payload. Every other key in the payload is ignored and never copied.
// Returns false when the text is not JSON or carries no usable window;
// `out` is left in the fail-closed empty state in that case.
bool ParseStatusLinePayload(const std::string& jsonText, UsageSnapshot& out, std::string* error);

// Same extraction, given an already-parsed document.
bool ExtractUsage(const struct JsonValue& root, UsageSnapshot& out);

// Clamps and rounds one used_percentage value. Returns false for NaN, inf, or
// a non-numeric source.
bool NormalizeUsedPercent(double raw, int& outUsed);

// Accepts seconds or milliseconds since the epoch, but only inside the valid
// range. Returns false for anything else.
bool NormalizeEpoch(double raw, long long& outSeconds);

// "73", "100", or "--" when there is no usable five-hour figure.
std::string FormatTrayLabel(const UsageSnapshot& snapshot);

bool IsStale(const UsageSnapshot& snapshot, long long nowUnix);

// "Today 3:00 PM (in 1h 22m)" style local-time text, or "Unavailable".
std::string FormatResetTime(const RateWindow& window, long long nowUnix);

// "2 minutes ago" style text for the panel footer.
std::string FormatRelativeAge(long long receivedAtUnix, long long nowUnix);

long long NowUnix();

}  // namespace cwut
