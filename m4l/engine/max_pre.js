// Prepended to the Emscripten output (build.py --pre-js). Max's v8 may have no console:
// send the runtime's messages to the Max Console instead.
if (typeof console === "undefined" && typeof post === "function") {
	globalThis.console = {
		log: function () { post(Array.prototype.join.call(arguments, " ") + "\n"); },
	};
	console.warn = console.error = console.log;
}
