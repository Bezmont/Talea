# Talea for Max for Live: what's left

The order the remaining work is done in. Each item is tested in Live before the next one
starts; done items move to the list at the bottom. The README describes what's built.

## Next

1. **Commit** the work tested so far: REC / OVD and monitoring, the SONG page, COPY / PASTE /
   CLEAR, channels, groups (once tested in Live).
2. **Step pools**: on KEYS, a selected step gets its own note pool (a chord or one note) in
   place of the track's, as on Rack (a pool lock; COPY / PASTE on a step already carries it).
3. **Cycles, the rest**: a cycle's own scale, root, pool and random amounts (in Cycle edit
   the SCALE, KEYS and RANDOM pages edit the cycle's copy), and Rack's lanes of every cycle
   on the TRACK page.
4. **All**: Rack's All button, every track edited together (a group of all 16).
5. **Temp**: Rack's temporary edits, reverted on release or a pattern switch.

## Then: the panel

6. Saved with the set: the pad mode, the KEYS octave.
7. A "display follows dials" switch (Rack's setting), on by default.
8. Mouse wheel on the dials, through `[mousestate]` (as the pads' right-click).
9. Tidying the display's tabs and rows.
10. The pop-out window: the full step grid (a row per 16 steps), track select along the
    top, the KEYS click keyboard along the bottom.

## Then: Info View text (only once all UI work is done)

- Every control gets Live Info View text (`annotation_name` / `annotation`), taken from
  the Rack version's established tooltips and adapted to what the control does in Live.
  Not before the UI work above is finished. (COPY, PASTE, CLEAR and Apply have
  placeholder text for now.)

## Then: release

11. Freezing the devices (the `.js` files inside the `.amxd`, not beside it).
12. Testing on macOS.
13. Merging branch `m4l` into `main`.
14. The manual in JezricWeb (the user's).

## Waiting or open

- **Harmony** dial: on hold.
- **Shared pitches on a channel**: two tracks on one channel playing the same pitch can end
  each other's note (as on Rack). A count per pitch, if it's needed.
- **Receiver setting name**: it's now "Channel"; receivers in sets saved with "Track" come
  back on channel 1.
- **For the next Rack revision** (`docs/SPEC.md` §6, only when asked): the per-pattern track
  channel; track COPY / CLEAR including step locks.

## Done

- Sequencer and receiver devices, timing, the bank saved with the set, undo.
- Panel: On, dials, track select, step pads, Mute and Pattern pad modes.
- Display pages: TRACK, LOCKS, CYCLE, KEYS, SCALE, PHRASE, VOICING, GROOVE, RANDOM,
  PATTERN, SETUP, Mute view; diagrams.
- Step locks; SEQ mode; REC / OVD and monitoring.
- SONG page (song mode on Live's timeline).
- COPY / PASTE / CLEAR.
- Channels: a receiver plays a channel, tracks set theirs on SETUP.
- Groups (in testing): track buttons in place of the tab; dials and page settings edit the
  group; COPY / PASTE / CLEAR of a group, pasted on tracks or into pattern slots.
