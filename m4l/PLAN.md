# Talea for Max for Live: what's left

The order the remaining work is done in. Each item is tested in Live before the next one
starts; done items move to the list at the bottom. The README describes what's built.

## Next

1. ~~Commit~~ done (REC / OVD, SONG, COPY / PASTE / CLEAR, channels, groups; then step
   pools, the rest of cycles, Temp and the DEVICE page).
2. **The floating window** (in testing; with DEVICE's WINDOW size, PAGE LOCK and PAGE
   SYNC): a copy of the whole panel in a window floating
   over Live, so it stays when another track is chosen; stacked (panel, display at the
   panel's width, an always-there keyboard). The window button right of On (TRACKS label
   now TRK). On as a power sign; the pin (window on top or not) right of the window
   button (in testing). Later, if wanted: the full step grid in it.
3. **The pads replaced** (in testing): STEPS' board clicked as the pads (TRACK renamed
   STEPS); pattern buttons over the track buttons (Ctrl+click chains one, Shift+click a range; a
   COPY / PASTE / CLEAR target), the PATTERN page gone (SWITCH on DEVICE: PAT SWITCH);
   SONG's Ctrl+click on PATTERN adds an entry; ENTRIES as one button per entry; MUTE
   TRACKS on the track buttons, APPLY under it; the left column re-laid (On + TRACKS,
   window + RECORD, pin + OVERDUB, CPY PST CLR, MUTE TRACKS, APPLY, TEMP MODE); the pads,
   pad mode menu, step page tabs and Follow gone; the tab order changed.

## Then: the panel

4. Saved with the set: the KEYS octave (and MUTE TRACKS, if wanted).
5. Mouse wheel on the dials, through `[mousestate]` (as right-clicks).
6. Tidying the display's tabs and rows.

## Then: release

7. Freezing the devices (the `.js` files inside the `.amxd`, not beside it). Freezing
   needs Max, so CI can't do it: the release zip would then take the frozen devices.
8. Testing on macOS.
9. Merging branch `m4l` into `main`.
10. The manual in JezricWeb (the user's).

Release builds are in CI (`.github/workflows/build.yml`): the `m4l` job builds the engine,
runs the simulator and zips the devices with their scripts (`Talea-M4L-<version>.zip`);
a `v*` tag matching `plugin.json`'s version publishes it and the four `.vcvplugin` files
as a GitHub Release.

## Waiting or open

- **Harmony** dial: on hold.
- **All** (back burner): Rack's All button, every track edited together (a group of all
  16; dial turns relative on each track). Skipped for now.
- **Shared pitches on a channel**: two tracks on one channel playing the same pitch can end
  each other's note (as on Rack). A count per pitch, if it's needed.
- **Receiver setting name**: it's now "Channel"; receivers in sets saved with "Track" come
  back on channel 1.
- **For the next Rack revision** (`docs/SPEC.md` §6, only when asked): the per-pattern track
  channel; track COPY / CLEAR including step locks. In §6 now: STYLE page → VOICING; Temp
  off quantized (TEMP OFF).

## Done

- Sequencer and receiver devices, timing, the bank saved with the set, undo.
- Panel: On, dials, track select, step pads, Mute and Pattern pad modes.
- Display pages (in tab order): STEPS, CYCLE, SCALE, KEYS (device only),
  GROOVE, VOICING, PHRASE, LOCKS, RANDOM, SONG, SETUP, then the gear; Mute view; diagrams.
- Step locks; SEQ mode; REC / OVD and monitoring.
- SONG page (song mode on Live's timeline).
- COPY / PASTE / CLEAR.
- Channels: a receiver plays a channel, tracks set theirs on SETUP.
- Groups: track buttons in place of the tab; dials and page settings edit the
  group; COPY / PASTE / CLEAR of a group, pasted on tracks or into pattern slots.
- Step pools (KEYS with steps selected; LOCKS' POOL lane).
- Info View text for every control, device and window (`device/jezric_talea_info.js`,
  adapted from Rack's tooltips).
- Cycles: a cycle's own scale, root, pool, phrase and random amounts in Cycle edit;
  STEPS' cycle lanes (a label click edits that cycle), PAGE / FOLLOW there.
- Temp (TEMP right of Follow; TEMP TO); the DEVICE page (the gear): TEMP OFF, Instance,
  Ahead.
