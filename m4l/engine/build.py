"""Builds Talea's engine as WebAssembly for Max's v8 object.

    python m4l/engine/build.py [--emsdk D:/dev/emsdk] [--debug]

Writes m4l/engine/build/talea_engine.js: one file with the WebAssembly embedded in it.

Max's v8 runs WebAssembly, but only synchronously: promises never resolve there (checked
in Max 9.0.10). So the module compiles synchronously, is embedded in the script rather
than fetched, and isn't wrapped in Emscripten's promise-returning factory. After the
script has run, `Module` is ready to use.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = HERE / "build" / "talea_engine.js"

EXPORTS = [
    "create", "destroy", "num_tracks", "num_params",
    "param_key", "param_name", "param_min", "param_max", "param_default",
    "set_param", "get_param", "load_pattern", "save_pattern",
    "process", "all_notes_off", "rewind", "set_muted",
    "save_playback", "load_playback", "playback_slots",
    "event_time", "event_track", "event_on", "event_note", "event_velocity",
    "event_chord_size", "event_chord_note", "event_cc", "event_glide",
    "toggle_step", "steps", "played_steps", "hits_low", "hits_high", "step_at",
    "jump", "commit_mutes", "select_pattern", "perf", "save_bank", "load_bank",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emsdk", default=os.environ.get("EMSDK", "D:/dev/emsdk"))
    ap.add_argument("--debug", action="store_true", help="-O0 with assertions")
    args = ap.parse_args()

    emxx = Path(args.emsdk) / "upstream" / "emscripten" / ("em++.exe" if os.name == "nt" else "em++")
    if not emxx.exists():
        sys.exit(f"em++ not found at {emxx}: install emsdk or pass --emsdk")

    sources = sorted(str(p) for p in (ROOT / "src" / "core").glob("*.cpp"))
    sources.append(str(HERE / "talea_wasm.cpp"))
    exported = ",".join(["_malloc", "_free"] + ["_talea_" + e for e in EXPORTS])
    OUT.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(emxx), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-O0" if args.debug else "-O2",
        f"-I{ROOT / 'src'}",
        *sources,
        "-o", str(OUT),
        "-sENVIRONMENT=shell",          # neither a browser nor Node: no fetch, no files
        "-sWASM_ASYNC_COMPILATION=0",   # Max's v8 doesn't resolve promises
        "-sSINGLE_FILE=1",              # the WebAssembly embedded in the .js,
        "-sSINGLE_FILE_BINARY_ENCODE=0",  # as base64: plain ASCII (raw bytes broke Max's File)
        "-sFILESYSTEM=0",
        f"--pre-js={HERE / 'max_pre.js'}",          # console -> Max Console
        f"--extern-post-js={HERE / 'max_post.js'}", # module.exports for require()
        # Fixed memory: growable memory needs Memory.toResizableBuffer(), newer than
        # Max's v8. An engine with its pattern bank is several MB; 64 MB leaves room
        # for a few, and running out aborts loudly rather than corrupting anything.
        "-sALLOW_MEMORY_GROWTH=0",
        "-sINITIAL_MEMORY=67108864",
        "-sSTACK_SIZE=1048576",         # Pattern values are copied on the stack
        f"-sEXPORTED_FUNCTIONS={exported}",
        "-sEXPORTED_RUNTIME_METHODS=cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8",
    ]
    if args.debug:
        cmd += ["-sASSERTIONS=2"]
    print(" ".join(cmd[:1] + ["..."] + cmd[-12:]))
    env = dict(os.environ, EMSDK=str(args.emsdk))
    # em++.exe runs Python from EMSDK_PYTHON or PATH; emsdk ships one, for machines
    # without Python on PATH.
    if "EMSDK_PYTHON" not in env:
        bundled = sorted((Path(args.emsdk) / "python").glob("*/python.exe"))
        if bundled:
            env["EMSDK_PYTHON"] = str(bundled[-1])
    subprocess.run(cmd, check=True, env=env)
    print(f"wrote {OUT} ({OUT.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
