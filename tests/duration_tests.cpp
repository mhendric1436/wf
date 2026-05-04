#include "catch2/catch_amalgamated.hpp"
#include "wf/duration.hpp"

#include <chrono>
#include <stdexcept>

using workflow::calculateLeaseDuration;
using workflow::parseIso8601DurationToSeconds;

TEST_CASE("parseIso8601DurationToSeconds parses seconds")
{
    REQUIRE(parseIso8601DurationToSeconds("PT1S") == std::chrono::seconds{1});
    REQUIRE(parseIso8601DurationToSeconds("PT30S") == std::chrono::seconds{30});
    REQUIRE(parseIso8601DurationToSeconds("PT120S") == std::chrono::seconds{120});
}

TEST_CASE("parseIso8601DurationToSeconds parses minutes")
{
    REQUIRE(parseIso8601DurationToSeconds("PT1M") == std::chrono::seconds{60});
    REQUIRE(parseIso8601DurationToSeconds("PT5M") == std::chrono::seconds{300});
    REQUIRE(parseIso8601DurationToSeconds("PT10M") == std::chrono::seconds{600});
}

TEST_CASE("parseIso8601DurationToSeconds parses hours and days")
{
    REQUIRE(parseIso8601DurationToSeconds("PT1H") == std::chrono::seconds{3600});
    REQUIRE(parseIso8601DurationToSeconds("PT2H") == std::chrono::seconds{7200});
    REQUIRE(parseIso8601DurationToSeconds("P1D") == std::chrono::seconds{86400});
    REQUIRE(parseIso8601DurationToSeconds("P2D") == std::chrono::seconds{172800});
}

TEST_CASE("parseIso8601DurationToSeconds parses compound durations")
{
    REQUIRE(parseIso8601DurationToSeconds("PT1M30S") == std::chrono::seconds{90});
    REQUIRE(parseIso8601DurationToSeconds("PT1H30M") == std::chrono::seconds{5400});
    REQUIRE(parseIso8601DurationToSeconds("PT1H2M3S") == std::chrono::seconds{3723});
    REQUIRE(parseIso8601DurationToSeconds("P1DT2H3M4S") == std::chrono::seconds{93784});
}

TEST_CASE("parseIso8601DurationToSeconds rejects invalid duration strings")
{
    REQUIRE_THROWS_AS(parseIso8601DurationToSeconds(""), std::invalid_argument);
    REQUIRE_THROWS_AS(parseIso8601DurationToSeconds("30S"), std::invalid_argument);
    REQUIRE_THROWS_AS(parseIso8601DurationToSeconds("PT"), std::invalid_argument);
    REQUIRE_THROWS_AS(parseIso8601DurationToSeconds("P"), std::invalid_argument);
    REQUIRE_THROWS_AS(parseIso8601DurationToSeconds("PT0S"), std::invalid_argument);
    REQUIRE_THROWS_AS(parseIso8601DurationToSeconds("PT-1S"), std::invalid_argument);
    REQUIRE_THROWS_AS(parseIso8601DurationToSeconds("PT1.5S"), std::invalid_argument);
    REQUIRE_THROWS_AS(parseIso8601DurationToSeconds("P1Y"), std::invalid_argument);
    REQUIRE_THROWS_AS(parseIso8601DurationToSeconds("P1W"), std::invalid_argument);
    REQUIRE_THROWS_AS(parseIso8601DurationToSeconds("not-a-duration"), std::invalid_argument);
}

TEST_CASE("calculateLeaseDuration returns one third of expected execution time")
{
    REQUIRE(calculateLeaseDuration("PT30S") == std::chrono::seconds{10});
    REQUIRE(calculateLeaseDuration("PT3M") == std::chrono::seconds{60});
    REQUIRE(calculateLeaseDuration("PT1H") == std::chrono::seconds{1200});
    REQUIRE(calculateLeaseDuration("P1D") == std::chrono::seconds{28800});
}

TEST_CASE("calculateLeaseDuration truncates fractional seconds from division")
{
    REQUIRE(calculateLeaseDuration("PT10S") == std::chrono::seconds{3});
    REQUIRE(calculateLeaseDuration("PT11S") == std::chrono::seconds{3});
    REQUIRE(calculateLeaseDuration("PT59S") == std::chrono::seconds{19});
}

TEST_CASE("calculateLeaseDuration enforces minimum one second lease")
{
    REQUIRE(calculateLeaseDuration("PT1S") == std::chrono::seconds{1});
    REQUIRE(calculateLeaseDuration("PT2S") == std::chrono::seconds{1});
}

TEST_CASE("calculateLeaseDuration rejects invalid durations")
{
    REQUIRE_THROWS_AS(calculateLeaseDuration(""), std::invalid_argument);
    REQUIRE_THROWS_AS(calculateLeaseDuration("PT"), std::invalid_argument);
    REQUIRE_THROWS_AS(calculateLeaseDuration("PT0S"), std::invalid_argument);
    REQUIRE_THROWS_AS(calculateLeaseDuration("not-a-duration"), std::invalid_argument);
}
