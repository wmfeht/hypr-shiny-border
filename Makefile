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

.PHONY: all clean clangd

all: $(PLUGIN).so

obj/%.o: src/%.cpp src/*.hpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(PLUGIN).so: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -rf obj $(PLUGIN).so compile_flags.txt

clangd:
	@printf '%s\n' $(CXXFLAGS) | tr ' ' '\n' | grep -v '^$$' > compile_flags.txt
	@echo 'wrote compile_flags.txt'
