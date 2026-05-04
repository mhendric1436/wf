#pragma once

#include <chrono>
#include <string>

namespace workflow
{

std::chrono::seconds parseIso8601DurationToSeconds(const std::string& value);

std::chrono::seconds calculateLeaseDuration(const std::string& expectedExecutionTime);

} // namespace workflow
