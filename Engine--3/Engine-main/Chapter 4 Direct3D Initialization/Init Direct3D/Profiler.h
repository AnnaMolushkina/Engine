#pragma once

// Единая точка подключения профайлера Tracy.
// TRACY_ENABLE приходит от цели Tracy::TracyClient (vcpkg); без него все макросы
// (ZoneScopedN, FrameMark, TracyPlot, ...) раскрываются в пустоту.
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
