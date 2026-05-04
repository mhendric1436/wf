#include "wf/duration.hpp"

#include <regex>
#include <stdexcept>

namespace workflow {

std::chrono::seconds parseIso8601DurationToSeconds(const std::string& value) {
    static const std::regex pattern(
        R"(^P(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)?$)"
    );

    std::smatch match;

    if (!std::regex_match(value, match, pattern)) {
        throw std::invalid_argument("invalid ISO-8601 duration: " + value);
    }

    const auto parsePart = [&match](std::size_t index) -> long long {
        if (index >= match.size() || !match[index].matched) {
            return 0;
        }

        return std::stoll(match[index].str());
    };

    const auto days = parsePart(1);
    const auto hours = parsePart(2);
    const auto minutes = parsePart(3);
    const auto seconds = parsePart(4);

    const auto totalSeconds =
        seconds +
        minutes * 60 +
        hours * 60 * 60 +
        days * 24 * 60 * 60;

    if (totalSeconds <= 0) {
        throw std::invalid_argument("duration must be greater than zero: " + value);
    }

    return std::chrono::seconds{totalSeconds};
}

std::chrono::seconds calculateLeaseDuration(
    const std::string& expectedExecutionTime
) {
    const auto expected = parseIso8601DurationToSeconds(expectedExecutionTime);
    const auto lease = expected / 3;

    if (lease < std::chrono::seconds{1}) {
        return std::chrono::seconds{1};
    }

    return lease;
}

} // namespace workflow
