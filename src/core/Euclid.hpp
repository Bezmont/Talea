#pragma once
#include <cstdint>

namespace talea {

constexpr int kMaxSteps = 64;

/** One bit per step: bit i set = hit on step i. */
using StepMask = uint64_t;

/** Mask with the lowest `steps` bits set. */
StepMask fullMask(int steps);

/** Euclidean rhythm (Bjorklund / Toussaint): `pulses` hits spread as evenly as possible
over `steps`, first hit on step 0. Arguments are clamped to 0..kMaxSteps and 0..steps. */
StepMask euclid(int steps, int pulses);

/** Rotates a `steps`-long pattern later in time: the hit on step i moves to step i + amount
(wrapping). Negative amounts rotate earlier. */
StepMask rotate(StepMask mask, int steps, int amount);

inline bool hasHit(StepMask mask, int step) {
	return (mask >> step) & 1u;
}

} // namespace talea
