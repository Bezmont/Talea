#include "core/Euclid.hpp"

#include <algorithm>

namespace talea {

StepMask fullMask(int steps) {
	if (steps <= 0)
		return 0;
	if (steps >= 64)
		return ~StepMask(0);
	return (StepMask(1) << steps) - 1;
}

StepMask euclid(int steps, int pulses) {
	steps = std::clamp(steps, 0, kMaxSteps);
	pulses = std::clamp(pulses, 0, steps);
	if (pulses == 0)
		return 0;
	if (pulses == steps)
		return fullMask(steps);

	// Toussaint's formulation: start with `pulses` groups [x] and `steps - pulses` groups [.],
	// then repeatedly append one remainder group to each leading group until at most one
	// remainder group is left. Fixed-size buffers: no allocation, safe on the audio thread.
	struct Group {
		StepMask bits;
		int len;
	};
	Group a[kMaxSteps];
	Group b[kMaxSteps];
	int na = pulses;
	int nb = steps - pulses;
	for (int i = 0; i < na; i++)
		a[i] = {1, 1};
	for (int i = 0; i < nb; i++)
		b[i] = {0, 1};

	while (nb > 1) {
		int m = std::min(na, nb);
		for (int i = 0; i < m; i++) {
			a[i].bits |= b[i].bits << a[i].len;
			a[i].len += b[i].len;
		}
		// Whatever wasn't paired becomes the new remainder.
		int nr;
		if (na > m) {
			nr = na - m;
			for (int i = 0; i < nr; i++)
				b[i] = a[m + i];
		}
		else {
			nr = nb - m;
			for (int i = 0; i < nr; i++)
				b[i] = b[m + i];
		}
		na = m;
		nb = nr;
	}

	StepMask out = 0;
	int pos = 0;
	for (int i = 0; i < na; i++) {
		out |= a[i].bits << pos;
		pos += a[i].len;
	}
	for (int i = 0; i < nb; i++) {
		out |= b[i].bits << pos;
		pos += b[i].len;
	}
	return out;
}

StepMask rotate(StepMask mask, int steps, int amount) {
	if (steps <= 0)
		return 0;
	steps = std::min(steps, kMaxSteps);
	mask &= fullMask(steps);
	int r = ((amount % steps) + steps) % steps;
	if (r == 0)
		return mask;
	return ((mask << r) | (mask >> (steps - r))) & fullMask(steps);
}

} // namespace talea
