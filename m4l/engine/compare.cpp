// Plays one busy scenario and prints every event, so the native build (what Rack runs) and
// the WebAssembly build (what Max runs) can be compared line by line. compare.py builds it
// both ways and diffs the output; any difference is a floating-point or compiler mismatch.

#include "core/Engine.hpp"

#include <cstdio>
#include <memory>

using namespace talea;

int main() {
	auto engine = std::make_unique<Engine>(); // too big for the stack
	Engine& e = *engine;
	Pattern& p = e.pattern;
	auto set = [&](int t, TrackParam param, float v) { p.setBase(t, param, v); };

	// Track 1: plain 16ths with swing and probability.
	set(0, TP_PULSES, 11);
	set(0, TP_TIMING, 45);
	set(0, TP_PROBABILITY, 30);
	// Track 2: 5 of 13 in triplet 16ths, repeats with ramp and pace, accents.
	set(1, TP_STEPS, 13);
	set(1, TP_PULSES, 5);
	set(1, TP_DIVISION, 8);
	set(1, TP_REPEATS, 3);
	set(1, TP_RAMP, -40);
	set(1, TP_PACE, 35);
	set(1, TP_ACCENT, 60);
	// Track 3: a chord pool with voicing, range and random pitch / velocity / sustain.
	set(2, TP_STEPS, 7);
	set(2, TP_PULSES, 4);
	set(2, TP_DIVISION, 5);
	set(2, TP_VOICING, 5);
	set(2, TP_RANGE, 19);
	set(2, TP_DELAY, 20);
	{
		Track& t = p.tracks[2];
		const int pool[] = {57, 60, 64, 67, 71};
		for (int i = 0; i < 5; i++)
			t.pool[i] = pool[i];
		t.poolSize = 5;
		p.random[2].amount[TP_PITCH] = 40;
		p.random[2].amount[TP_VELOCITY] = 70;
		p.random[2].amount[TP_SUSTAIN] = 50;
		p.random[2].variance = 30;
	}
	// Track 4: a finite Length that cuts the loop, Tail repeats, random rotate.
	set(3, TP_STEPS, 16);
	set(3, TP_PULSES, 6);
	set(3, TP_LENGTH, 7); // 2 bars
	set(3, TP_REPEATS, 2);
	set(3, TP_REPEAT_TIME, 4);
	p.tracks[3].repeatTail = true;
	p.random[3].amount[TP_ROTATE] = 50;
	p.random[3].amount[TP_PROBABILITY] = 25;

	EventList out;
	int lines = 0;
	auto run = [&](double from, double to) {
		out.clear();
		e.process(from, to, out);
		for (const NoteEvent& ev : out) {
			std::printf("%.12f %d %d %d %d %d", ev.time, ev.track, ev.on ? 1 : 0, ev.note, ev.velocity, ev.chordSize);
			for (int j = 1; j < ev.chordSize; j++)
				std::printf(" %d", ev.chord[j]);
			std::printf("\n");
			lines++;
		}
	};

	// 64 bars in uneven windows (like audio blocks at a varying tempo), then a jump back to
	// bar 9 and 16 more bars a 16th at a time (like the brain).
	double t = -1e-9;
	for (int i = 0; t < 256.0; i++) {
		double w = 0.013 + 0.011 * (i % 7);
		run(t, t + w);
		t += w;
	}
	std::printf("jump\n");
	for (int s = 32; s < 96; s++)
		run(s * 0.25 - 1e-9, (s + 1) * 0.25 - 1e-9);
	std::fprintf(stderr, "%d events\n", lines);
	return 0;
}
