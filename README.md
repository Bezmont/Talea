# Talea

A 16-track multi-mode sequencer (step, record, Euclidean / algorithmic) and composition tool for VCV Rack 2.
Design and behaviour: [docs/SPEC.md](docs/SPEC.md).

Inspired in part by hardware algorithmic and manual sequencers, including the Torso T-1 and Arturia Keystep. Not
affiliated with or endorsed by Torso Electronics or Arturia.

## Layout

```
src/core/     host-independent sequencer core (no Rack headers) — see SPEC §8
src/rack/     VCV Rack module, widgets, generated PanelLayout.hpp
tests/        core unit tests (built without Rack)
tools/        gen_panel.py: panel SVG, previews and PanelLayout.hpp
res/          panel SVG
```

## Build (MSYS2 MINGW64 shell)

Requires `RACK_DIR` pointing at the Rack SDK (2.6.x).

```sh
make test          # build and run core unit tests
make -j8           # build plugin.dll
make install       # package and copy to the Rack user plugins folder
make panel         # regenerate res/Talea.svg, docs/panel/* and src/rack/PanelLayout.hpp
```

Rack 2.6+ keeps its user folder in `%LOCALAPPDATA%\Rack2`. The MSYS2 login shell may not
export `LOCALAPPDATA`, so pass the folder explicitly:

```sh
make install RACK_USER_DIR=/c/Users/<you>/AppData/Local/Rack2
```

Panel positions and labels come from `tools/gen_panel.py`; edit the generator, not the
SVG or `PanelLayout.hpp`.
