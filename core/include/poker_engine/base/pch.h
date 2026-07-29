// Precompiled header for the poker engine core.
// Only includes what core's translation units actually have on their
// include path (fmt is linked PRIVATE; standard headers always are).
// spdlog / nlohmann_json are consumed elsewhere and intentionally
// excluded here to keep the PCH conflict-free.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <random>
#include <algorithm>

#include "fmt/core.h"
