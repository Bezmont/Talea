#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelTalea);
	p->addModel(modelTaleaOut);
	p->addModel(modelTaleaIn);
}
