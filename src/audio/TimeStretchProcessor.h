#pragma once

#if MTCONTROLS_USE_BUNGEE
#include "BungeeProcessor.h"
using TimeStretchProcessor = BungeeProcessor;
#else
#include "../PitchShiftProcessor.h"
using TimeStretchProcessor = PitchShiftProcessor;
#endif
