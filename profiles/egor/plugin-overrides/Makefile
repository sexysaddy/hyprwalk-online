.DEFAULT_GOAL := all
PLUGIN   := hyprwalk.so
SRC      := $(wildcard src/*.cpp)
OBJ      := $(SRC:src/%.cpp=build/%.o)
DEP       = $(OBJ:.o=.d)

PKGS     := hyprland pixman-1 libdrm glesv2 egl cairo pangocairo hyprgraphics
# hyprgraphics: the apps' icons, SVG too (Hyprland has it loaded already)
LIBS     := glesv2 egl cairo pangocairo hyprgraphics
# the microphone for lip sync (src/mic.cpp), when build.sh found PipeWire
ifeq ($(shell pkg-config --exists libpipewire-0.3 && echo yes),yes)
PKGS     += libpipewire-0.3
LIBS     += libpipewire-0.3
PIPEWIRE := -DHYPRWALK_PIPEWIRE
endif
# Optional in-process MP4/WebM decoding for the screen-space video viewmodel. Without these dev packages,
# Hyprwalk still builds and reports that video hands are unavailable rather than failing the whole plugin build.
ifeq ($(shell pkg-config --exists gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 && echo yes),yes)
PKGS     += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0
LIBS     += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0
GSTREAMER := -DHYPRWALK_GSTREAMER
endif
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++26 -fPIC -fno-gnu-unique -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
            -DWLR_USE_UNSTABLE $(PIPEWIRE) $(GSTREAMER) $(shell pkg-config --cflags $(PKGS))
LDFLAGS  += -shared
# libraries after the objects: with --as-needed (Debian's and Ubuntu's default) a library named before its users is
# dropped
LDLIBS   += $(shell pkg-config --libs $(LIBS))

# One source tree, but objects are always local to this fork. No new runtime library.
HYPRWALK_WEBRTC ?= 0
HYPRWALK_MULTIPLAYER ?= 1
HYPRWALK_MULTIPLAYER_DIR ?= ../hyprwalk-multiplayer
# MP headers are also used by some shared declarations in offline builds, but
# no multiplayer objects or feature macro are enabled unless requested.
CPPFLAGS += -I$(HYPRWALK_MULTIPLAYER_DIR)/include
ifeq ($(HYPRWALK_MULTIPLAYER),1)
CPPFLAGS += -DHYPRWALK_MULTIPLAYER
CXXFLAGS += -pthread
LDLIBS += -pthread
OBJ += build/multiplayer/VideoSignaling.o build/multiplayer/Multiplayer.o build/multiplayer/Protocol.o build/multiplayer/UdpProtocol.o build/multiplayer/UdpTransport.o build/multiplayer/PlayerSync.o build/multiplayer/MapSync.o build/multiplayer/ContentHash.o build/multiplayer/OnlineMenu.o build/multiplayer/WindowSync.o build/multiplayer/WindowMenu.o
# Optional media: isolated from the TCP/UDP objects and Egor's local video viewmodel.
ifeq ($(HYPRWALK_WEBRTC),1)
MEDIA_PKGS := gstreamer-1.0 gstreamer-app-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 gstreamer-video-1.0
ifeq ($(shell pkg-config --atleast-version=1.22 gstreamer-1.0 && pkg-config --exists $(MEDIA_PKGS) && echo yes),yes)
CPPFLAGS += -DHYPRWALK_WEBRTC -I$(HYPRWALK_MULTIPLAYER_DIR) $(shell pkg-config --cflags $(MEDIA_PKGS))
LDLIBS += $(shell pkg-config --libs $(MEDIA_PKGS))
OBJ += build/multiplayer/media/MediaSession.o build/multiplayer/media/Readback.o
else
$(error WebRTC SDK >=1.22 missing; install media development packages or use HYPRWALK_WEBRTC=0)
endif
else ifneq ($(HYPRWALK_WEBRTC),0)
$(error HYPRWALK_WEBRTC must be 0 or 1)
endif
else ifneq ($(HYPRWALK_MULTIPLAYER),0)
$(error HYPRWALK_MULTIPLAYER must be 0 or 1)
endif

all: $(PLUGIN)

$(PLUGIN): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.cpp Makefile | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

# Force a rebuild when toggling the feature or selecting a different module source tree.
# cmp preserves the stamp timestamp for unchanged settings, avoiding recompilation on every make.
$(OBJ): build/.multiplayer-config
build/.multiplayer-config: FORCE | build
	@printf '%s\n' '$(HYPRWALK_MULTIPLAYER)|$(HYPRWALK_WEBRTC)|$(abspath $(HYPRWALK_MULTIPLAYER_DIR))' > $@.tmp
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv -f $@.tmp $@

ifeq ($(HYPRWALK_MULTIPLAYER),1)
build/multiplayer/%.o: $(HYPRWALK_MULTIPLAYER_DIR)/src/%.cpp Makefile | build/multiplayer
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
endif
build/multiplayer: | build
	mkdir -p $@


FORCE:

build:
	mkdir -p build

clean:
	rm -rf build $(PLUGIN)

-include $(DEP)

.PHONY: all clean FORCE

ifeq ($(HYPRWALK_MULTIPLAYER)$(HYPRWALK_WEBRTC),11)
build/multiplayer/media/MediaSession.o: $(HYPRWALK_MULTIPLAYER_DIR)/media/gstreamer/MediaSession.cpp Makefile | build/multiplayer/media
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/multiplayer/media/Readback.o: $(HYPRWALK_MULTIPLAYER_DIR)/media/gl/Readback.cpp Makefile | build/multiplayer/media
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/multiplayer/media: | build/multiplayer
	mkdir -p $@
endif
