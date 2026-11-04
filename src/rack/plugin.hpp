#pragma once

// Newer GCC reports a false -Warray-bounds in Port::setChannels() when the channel count is
// a compile-time 16; silence it for Rack's headers only.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#include <rack.hpp>
#pragma GCC diagnostic pop

using namespace rack;

extern Plugin* pluginInstance;

extern Model* modelTalea;
extern Model* modelTaleaOut;
extern Model* modelTaleaIn;
