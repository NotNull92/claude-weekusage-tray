#include "usage.h"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "json.h"

namespace cwut {
namespace {

bool readWindow(const JsonValue* node, RateWindow& out) {
    out = RateWindow();
    if (node == nullptr || !node->isObject()) return false;

    if (const JsonValue* used = node->find("used_percentage")) {
        if (used->isNumber()) {
            int normalized = 0;
            if (NormalizeUsedPercent(used->number, normalized)) {
                out.usedPercent = normalized;
                out.hasUsage = true;
            }
        }
    }
    if (const JsonValue* reset = node->find("resets_at")) {
        if (reset->isNumber()) {
            long long seconds = 0;
            if (NormalizeEpoch(reset->number, seconds)) {
                out.resetsAt = seconds;
                out.hasReset = true;
            }
        }
    }
    return out.hasUsage;
}

std::string formatClock(const std::tm& tm) {
    int hour = tm.tm_hour % 12;
    if (hour == 0) hour = 12;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", hour, tm.tm_min,
                  tm.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

std::string formatDuration(long long seconds) {
    if (seconds < 60) return "less than a minute";
    long long minutes = seconds / 60;
    if (minutes < 60) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lldm", minutes);
        return buf;
    }
    long long hours = minutes / 60;
    minutes %= 60;
    char buf[48];
    if (hours < 24) {
        std::snprintf(buf, sizeof(buf), "%lldh %lldm", hours, minutes);
        return buf;
    }
    long long days = hours / 24;
    hours %= 24;
    std::snprintf(buf, sizeof(buf), "%lldd %lldh", days, hours);
    return buf;
}

bool localTime(long long epoch, std::tm& out) {
    std::time_t t = static_cast<std::time_t>(epoch);
    return localtime_s(&out, &t) == 0;
}

}  // namespace

bool NormalizeUsedPercent(double raw, int& outUsed) {
    if (!std::isfinite(raw)) return false;
    if (raw < 0.0) raw = 0.0;
    if (raw > 100.0) raw = 100.0;
    // Round up: a window that is 27.2% consumed leaves 72.8%, and reporting
    // 73% would promise headroom the user does not have.
    double ceiled = std::ceil(raw - 1e-9);
    if (ceiled < 0.0) ceiled = 0.0;
    if (ceiled > 100.0) ceiled = 100.0;
    outUsed = static_cast<int>(ceiled);
    return true;
}

bool NormalizeEpoch(double raw, long long& outSeconds) {
    if (!std::isfinite(raw)) return false;
    if (raw < 0.0) return false;
    double value = raw;
    // Some producers emit milliseconds. Accept that only when the value lands
    // squarely in the millisecond band for the same valid date range.
    if (value >= static_cast<double>(kMinEpochSeconds) * 1000.0 &&
        value <= static_cast<double>(kMaxEpochSeconds) * 1000.0) {
        value /= 1000.0;
    }
    if (value < static_cast<double>(kMinEpochSeconds)) return false;
    if (value > static_cast<double>(kMaxEpochSeconds)) return false;
    outSeconds = static_cast<long long>(value);
    return true;
}

bool ExtractUsage(const JsonValue& root, UsageSnapshot& out) {
    out = UsageSnapshot();
    if (!root.isObject()) return false;
    const JsonValue* limits = root.find("rate_limits");
    if (limits == nullptr || !limits->isObject()) return false;

    bool five = readWindow(limits->find("five_hour"), out.fiveHour);
    bool seven = readWindow(limits->find("seven_day"), out.sevenDay);
    return five || seven;
}

bool ParseStatusLinePayload(const std::string& jsonText, UsageSnapshot& out, std::string* error) {
    out = UsageSnapshot();
    JsonValue root;
    if (!JsonParse(jsonText, root, error)) return false;
    if (!ExtractUsage(root, out)) {
        if (error) *error = "payload carries no usable rate_limits window";
        return false;
    }
    return true;
}

std::string FormatTrayLabel(const UsageSnapshot& snapshot) {
    if (snapshot.receivedAtUnix == 0) return "--";
    if (!snapshot.fiveHour.hasUsage) return "--";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", snapshot.fiveHour.remainingPercent());
    return buf;
}

bool IsStale(const UsageSnapshot& snapshot, long long nowUnix) {
    if (snapshot.receivedAtUnix == 0) return true;
    return (nowUnix - snapshot.receivedAtUnix) > kStaleAfterSeconds;
}

std::string FormatResetTime(const RateWindow& window, long long nowUnix) {
    if (!window.hasReset) return "Unavailable";

    std::tm target{};
    std::tm now{};
    if (!localTime(window.resetsAt, target) || !localTime(nowUnix, now)) return "Unavailable";

    std::string day;
    if (target.tm_year == now.tm_year && target.tm_yday == now.tm_yday) {
        day = "Today";
    } else {
        std::tm tomorrow = now;
        std::time_t tomorrowT = static_cast<std::time_t>(nowUnix + 24 * 3600);
        if (localtime_s(&tomorrow, &tomorrowT) == 0 && target.tm_year == tomorrow.tm_year &&
            target.tm_yday == tomorrow.tm_yday) {
            day = "Tomorrow";
        } else {
            char buf[32];
            if (std::strftime(buf, sizeof(buf), "%b %d", &target) == 0) return "Unavailable";
            day = buf;
        }
    }

    std::string text = day + " " + formatClock(target);
    if (window.resetsAt > nowUnix) {
        text += " (in " + formatDuration(window.resetsAt - nowUnix) + ")";
    } else {
        text += " (already passed)";
    }
    return text;
}

std::string FormatRelativeAge(long long receivedAtUnix, long long nowUnix) {
    if (receivedAtUnix == 0) return "never";
    long long age = nowUnix - receivedAtUnix;
    if (age < 0) age = 0;
    if (age < 45) return "just now";
    return formatDuration(age) + " ago";
}

long long NowUnix() { return static_cast<long long>(std::time(nullptr)); }

}  // namespace cwut
