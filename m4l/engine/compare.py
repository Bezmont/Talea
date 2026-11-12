"""Checks the WebAssembly engine plays exactly what the native one does.

    python m4l/engine/compare.py [--emsdk D:/dev/emsdk]

Builds compare.cpp natively (MSYS2's g++ on Windows, like Rack's build) and with
Emscripten, runs both, and compares their events line by line.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
BUILD = HERE / "build"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emsdk", default=os.environ.get("EMSDK", "D:/dev/emsdk"))
    ap.add_argument("--cxx", default="C:/msys64/mingw64/bin/g++.exe" if os.name == "nt" else "g++")
    args = ap.parse_args()
    BUILD.mkdir(exist_ok=True)
    sources = sorted(str(p) for p in (ROOT / "src" / "core").glob("*.cpp")) + [str(HERE / "compare.cpp")]
    flags = ["-std=c++17", "-O2", f"-I{ROOT / 'src'}"]

    native = BUILD / ("compare_native.exe" if os.name == "nt" else "compare_native")
    env = dict(os.environ)
    if os.name == "nt":
        env["PATH"] = str(Path(args.cxx).parent) + os.pathsep + env["PATH"]  # its DLLs
    subprocess.run([args.cxx, *flags, *sources, "-o", str(native)], check=True, env=env)

    emxx = Path(args.emsdk) / "upstream" / "emscripten" / ("em++.exe" if os.name == "nt" else "em++")
    wasm = BUILD / "compare_wasm.js"
    subprocess.run([str(emxx), *flags, *sources, "-o", str(wasm), "-sENVIRONMENT=node",
                    "-sSTACK_SIZE=1048576", "-sINITIAL_MEMORY=67108864"],
                   check=True, env=dict(os.environ, EMSDK=str(args.emsdk)))
    node = next((Path(args.emsdk) / "node").glob("*/bin/node*"), None) or \
        next((Path(args.emsdk) / "node").glob("*/node.exe"))

    a = subprocess.run([str(native)], capture_output=True, text=True, check=True, env=env).stdout.splitlines()
    b = subprocess.run([str(node), str(wasm)], capture_output=True, text=True, check=True).stdout.splitlines()
    print(f"native {len(a)} lines, WebAssembly {len(b)} lines")
    diffs = [(i, x, y) for i, (x, y) in enumerate(zip(a, b)) if x != y]
    if len(a) != len(b) or diffs:
        for i, x, y in diffs[:10]:
            print(f"  line {i + 1}: native {x!r}\n           wasm   {y!r}")
        sys.exit("DIFFERENT")
    print("IDENTICAL")


if __name__ == "__main__":
    main()
