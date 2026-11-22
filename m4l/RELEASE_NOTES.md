> [!WARNING]
> **Not yet tested on macOS.** Talea for Max for Live has been developed and tested on
> Windows only. It should run the same on a Mac (nothing in it is platform-specific), but
> until it has been tried there, expect problems and please report them in
> [Issues](https://github.com/Bezmont/Talea/issues).

Talea, the 16-track Euclidean, algorithmic and step sequencer, as two Max MIDI Effects for
Ableton Live. It runs the same engine as the [VCV Rack module](https://library.vcvrack.com/Jezric).

## Requirements

- Ableton Live with Max for Live, and **Max 9** (bundled with Live 12.2 and later): the
  devices use Max 9's `v8` JavaScript object.

## Install

1. Download `Talea-M4L-<version>.zip` below and unzip it.
2. Move the `Talea` folder somewhere permanent, e.g. your Live User Library. Keep its files
   together: the devices load their scripts from their own folder.
3. In Live, drag **Talea Sequencer.amxd** onto a MIDI track (one per set).
4. Drag **Talea Track Receiver.amxd** onto each instrument track, before the instrument,
   and set its **Channel** (1-16) to the Talea tracks it should play.

Manual: https://www.jezric.com/software/talea
