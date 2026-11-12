// Appended to the Emscripten output (build.py --extern-post-js): hands the ready Module to
// require() where there is one; otherwise `Module` stays a global.
if (typeof module !== "undefined" && module.exports) module.exports = Module;
