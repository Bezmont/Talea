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
   now TRK). Later, if wanted: the full step grid in it.

## Then: the panel

3. Saved with the set: the pad mode, the KEYS octave.
4. Mouse wheel on the dials, through `[mousestate]` (as the pads' right-click).
5. Tidying the display's tabs and rows.

## Then: release

6. Freezing the devices (the `.js` files inside the `.amxd`, not beside it).
7. Testing on macOS.
8. Merging branch `m4l` into `main`.
9. The manual in JezricWeb (the user's).

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
- Display pages: TRACK, LOCKS, CYCLE, KEYS, SCALE, PHRASE, VOICING, GROOVE, RANDOM,
  PATTERN, SETUP, Mute view; diagrams.
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
  TRACK's cycle lanes (a label click edits that cycle), PAGE / FOLLOW there.
- Temp (TEMP right of Follow; TEMP TO); the DEVICE page (the gear): TEMP OFF, Instance,
  Ahead.
