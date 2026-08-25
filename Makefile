# Build against the Hyprland that pkg-config finds (Omarchy: /usr).
# --no-gnu-unique is required so the plugin can actually unload.

PLUGIN := hypr-shiny-border

CXX      ?= g++
PKG      := hyprland pixman-1 libdrm pangocairo
CXXFLAGS ?= -std=gnu++26 -O2 -g -fPIC -fno-gnu-unique
CXXFLAGS += -fdiagnostics-color=always -DWLR_USE_UNSTABLE
CXXFLAGS += $(shell pkg-config --cflags $(PKG))
LDFLAGS  ?= -shared
LIBS     := $(shell pkg-config --libs $(PKG))

SRC := $(wildcard src/*.cpp)
OBJ := $(patsubst src/%.cpp,obj/%.o,$(SRC))

TEST_TEARDOWN := tests/test_teardown
TEST_RUNTIME  := tests/test_runtime
TEST_RELOAD   := tests/test_reload
TEST_VISUAL   := tests/test_visual
TESTS         := $(TEST_TEARDOWN) $(TEST_RUNTIME) $(TEST_RELOAD) $(TEST_VISUAL)

.PHONY: all clean clangd test

all: $(PLUGIN).so

obj/%.o: src/%.cpp src/*.hpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(PLUGIN).so: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

test: $(PLUGIN).so $(TESTS)
	$(TEST_TEARDOWN)
	$(TEST_RUNTIME)
	$(TEST_RELOAD)
	$(TEST_VISUAL)

$(TEST_TEARDOWN): tests/test_teardown.cpp obj/teardown.o
	$(CXX) -std=gnu++26 -O2 -g -o $@ tests/test_teardown.cpp obj/teardown.o

$(TEST_RUNTIME): tests/test_runtime.cpp obj/runtime.o
	$(CXX) -std=gnu++26 -O2 -g -o $@ tests/test_runtime.cpp obj/runtime.o

$(TEST_RELOAD): tests/test_reload.cpp obj/runtime.o
	$(CXX) -std=gnu++26 -O2 -g -o $@ tests/test_reload.cpp obj/runtime.o

$(TEST_VISUAL): tests/test_visual.cpp obj/runtime.o
	$(CXX) -std=gnu++26 -O2 -g -o $@ tests/test_visual.cpp obj/runtime.o

clean:
	rm -rf obj $(PLUGIN).so compile_flags.txt $(TESTS)

clangd:
	@printf '%s\n' $(CXXFLAGS) | tr ' ' '\n' | grep -v '^$$' > compile_flags.txt
	@echo 'wrote compile_flags.txt'
