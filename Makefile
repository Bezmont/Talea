RACK_DIR ?= ../..

FLAGS += -Isrc

SOURCES += $(wildcard src/core/*.cpp)
SOURCES += $(wildcard src/rack/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk

# Rack's compile.mk sets -std=c++11; a later -std wins.
CXXFLAGS += -std=c++17

# ---------------------------------------------------------------- core tests
# Built with the host compiler only: no Rack headers or libraries (core rule: src/core is
# host-independent).

TEST_SOURCES := $(wildcard src/core/*.cpp) $(wildcard tests/*.cpp)
TEST_HEADERS := $(wildcard src/core/*.hpp) $(wildcard tests/*.hpp)
TEST_BIN := build/tests/talea_tests$(if $(ARCH_WIN),.exe,)

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SOURCES) $(TEST_HEADERS)
	@mkdir -p $(@D)
	$(CXX) -std=c++17 -O1 -g -Wall -Wextra -Werror -Isrc -o $@ $(TEST_SOURCES)

# Regenerate the panel SVG, previews and src/rack/PanelLayout.hpp.
panel:
	python tools/gen_panel.py

.PHONY: test panel
