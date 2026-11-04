#pragma once
#include "plugin.hpp"
#include "core/Model.hpp"

#include <cmath>
#include <mutex>

/** Native MIDI out (§3.13): one Rack MIDI output, a channel per track, full chords. Notes
follow the gates: a note-on first ends the track's previous notes, a note-off ends them all,
so nothing hangs. CC tracks send Control Change instead, only when the value changes.
Optional MIDI clock (24 PPQN) with Start / Continue / Stop.

Threads: the audio thread sends; the UI thread switches the device (context menu). A device
switch closes the old device, so it holds `mutex` while it ends the sounding notes on the old
device and switches; the audio thread only try-locks and skips its messages meanwhile, so it
never waits. The channel and clock settings are plain values the SETUP page writes. */
struct MidiNoteOut {
	std::mutex mutex;

	/** The port's own channel selector is hidden: each message carries its track's channel. */
	struct Port : midi::Output {
		MidiNoteOut* owner = nullptr;
		std::vector<int> getChannels() override {
			return {};
		}
		/** Also called for a driver change. */
		void setDeviceId(int deviceId) override {
			if (!owner) {
				midi::Output::setDeviceId(deviceId);
				return;
			}
			std::lock_guard<std::mutex> lock(owner->mutex);
			owner->allOffLocked();
			midi::Output::setDeviceId(deviceId);
			channel = -1;
			owner->forgetCcLocked(); // the new device gets the current values
		}
	} port;

	/** MIDI channel per track (0-15), -1 = off. Default: track n on channel n. */
	int channel[talea::kNumTracks];
	bool clock = false;

	MidiNoteOut() {
		for (int t = 0; t < talea::kNumTracks; t++)
			channel[t] = t;
		port.channel = -1;
		port.owner = this;
	}

	// Audio thread: each call skips its messages while a device switch holds the lock.

	void noteOn(int track, const uint8_t* chord, int n, int velocity) {
		std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
		if (!lock)
			return;
		offLocked(track);
		int ch = channel[track];
		if (ch < 0)
			return;
		for (int i = 0; i < n && i < talea::kMaxPool; i++) {
			send(0x90 | ch, chord[i], std::max(velocity, 1));
			notes_[track][i] = chord[i];
		}
		count_[track] = std::min(n, talea::kMaxPool);
		sentChannel_[track] = ch;
	}

	/** CC track value: sent when the value, number or channel changes. */
	void cc(int track, int number, int value) {
		std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
		int ch = channel[track];
		if (!lock || ch < 0)
			return;
		if (ccValue_[track] == value && ccNumber_[track] == number && ccChannel_[track] == ch)
			return;
		send(0xB0 | ch, number, value);
		ccValue_[track] = value;
		ccNumber_[track] = number;
		ccChannel_[track] = ch;
	}

	/** Ends the track's sounding notes, on the channel they started on. */
	void off(int track) {
		std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
		if (lock)
			offLocked(track);
	}

	/** All Notes Off (CC 123) on every channel, for stuck notes elsewhere. */
	void panic() {
		std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
		if (!lock)
			return;
		allOffLocked();
		for (int ch = 0; ch < 16; ch++)
			send(0xB0 | ch, 123, 0);
	}

	/** Transport start: Start from the top, Continue elsewhere. */
	void start(double beat) {
		std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
		if (!clock || !lock)
			return;
		send1(beat <= 0.0 ? 0xFA : 0xFB);
		clockCount_ = long(std::floor(beat * 24.0)) - 1;
	}

	void stop() {
		std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
		if (clock && lock)
			send1(0xFC);
	}

	/** Clock ticks up to `beat` (quarter notes). */
	void tick(double beat) {
		if (!clock)
			return;
		std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
		if (!lock)
			return;
		long c = long(std::floor(beat * 24.0));
		for (int i = 0; c > clockCount_ && i < 4; i++)
			send1(0xF8), clockCount_++;
		clockCount_ = std::max(clockCount_, c);
	}

	json_t* toJson() {
		json_t* j = json_object();
		json_object_set_new(j, "port", port.toJson());
		json_t* ch = json_array();
		for (int c : channel)
			json_array_append_new(ch, json_integer(c));
		json_object_set_new(j, "channels", ch);
		json_object_set_new(j, "clock", json_boolean(clock));
		return j;
	}

	void fromJson(json_t* j) {
		if (!j)
			return;
		if (json_t* p = json_object_get(j, "port"))
			port.fromJson(p);
		port.channel = -1;
		if (json_t* ch = json_object_get(j, "channels")) {
			for (int t = 0; t < talea::kNumTracks && t < int(json_array_size(ch)); t++)
				channel[t] = clamp(int(json_integer_value(json_array_get(ch, t))), -1, 15);
		}
		if (json_t* c = json_object_get(j, "clock"))
			clock = json_boolean_value(c);
	}

	/** Off the audio thread (module removal, Initialize): waits for the lock. */
	void allOff() {
		std::lock_guard<std::mutex> lock(mutex);
		allOffLocked();
	}

	std::string deviceName() {
		int id = port.getDeviceId();
		return id < 0 ? "NO DEVICE" : port.getDeviceName(id);
	}

private:
	void offLocked(int track) {
		for (int i = 0; i < count_[track]; i++)
			send(0x80 | sentChannel_[track], notes_[track][i], 0);
		count_[track] = 0;
	}

	void allOffLocked() {
		for (int t = 0; t < talea::kNumTracks; t++)
			offLocked(t);
	}

	void forgetCcLocked() {
		for (int& v : ccValue_)
			v = -1;
	}

	uint8_t notes_[talea::kNumTracks][talea::kMaxPool] = {};
	int count_[talea::kNumTracks] = {};
	int sentChannel_[talea::kNumTracks] = {};
	long clockCount_ = -1;
	int ccValue_[talea::kNumTracks] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
	int ccNumber_[talea::kNumTracks] = {};
	int ccChannel_[talea::kNumTracks] = {};

	void send(int status, int d1, int d2) {
		midi::Message m;
		m.bytes = {uint8_t(status), uint8_t(d1 & 0x7f), uint8_t(d2 & 0x7f)};
		port.sendMessage(m);
	}

	void send1(int status) {
		midi::Message m;
		m.setSize(1);
		m.bytes[0] = uint8_t(status);
		port.sendMessage(m);
	}
};
