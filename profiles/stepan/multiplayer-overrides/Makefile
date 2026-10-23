CXX ?= c++
BUILD_DIR ?= build
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Werror -pthread -MMD -MP
LDLIBS += -pthread
CORE := $(BUILD_DIR)/VideoSignaling.o $(BUILD_DIR)/Multiplayer.o $(BUILD_DIR)/Protocol.o $(BUILD_DIR)/UdpProtocol.o $(BUILD_DIR)/UdpTransport.o $(BUILD_DIR)/PlayerSync.o $(BUILD_DIR)/MapSync.o $(BUILD_DIR)/ContentHash.o $(BUILD_DIR)/OnlineMenu.o $(BUILD_DIR)/WindowSync.o $(BUILD_DIR)/WindowMenu.o

all: $(BUILD_DIR)/online_menu_test $(BUILD_DIR)/player_cli $(BUILD_DIR)/control_test $(BUILD_DIR)/control_cli $(BUILD_DIR)/udp_test $(BUILD_DIR)/player_sync_test $(BUILD_DIR)/map_test

$(BUILD_DIR)/%.o: src/%.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/control_test.o: tests/control_test.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/control_cli.o: tests/control_cli.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/control_test: $(CORE) $(BUILD_DIR)/control_test.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(BUILD_DIR)/control_cli: $(CORE) $(BUILD_DIR)/control_cli.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(BUILD_DIR):
	mkdir -p $@
test: $(BUILD_DIR)/online_menu_test $(BUILD_DIR)/control_test $(BUILD_DIR)/udp_test $(BUILD_DIR)/player_sync_test $(BUILD_DIR)/map_test
	$(BUILD_DIR)/control_test
	$(BUILD_DIR)/udp_test
	$(BUILD_DIR)/player_sync_test
	$(BUILD_DIR)/map_test
	$(BUILD_DIR)/online_menu_test

control-test: $(BUILD_DIR)/control_test
	$(BUILD_DIR)/control_test

process-test: $(BUILD_DIR)/control_cli
	python3 tests/process_test.py $(BUILD_DIR)/control_cli $(BUILD_DIR)/control_cli $(BUILD_DIR)/process-logs

clean:
	rm -rf $(BUILD_DIR)

-include $(CORE:.o=.d) $(BUILD_DIR)/control_test.d $(BUILD_DIR)/control_cli.d
.PHONY: all test clean control-test process-test

$(BUILD_DIR)/udp_test.o: tests/udp_test.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/udp_test: $(CORE) $(BUILD_DIR)/udp_test.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
udp-test: $(BUILD_DIR)/udp_test
	$(BUILD_DIR)/udp_test
-include $(BUILD_DIR)/udp_test.d
.PHONY: udp-test

$(BUILD_DIR)/player_sync_test.o: tests/player_sync_test.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/player_sync_test: $(CORE) $(BUILD_DIR)/player_sync_test.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
player-test: $(BUILD_DIR)/player_sync_test
	$(BUILD_DIR)/player_sync_test
-include $(BUILD_DIR)/player_sync_test.d
.PHONY: player-test

$(BUILD_DIR)/map_test.o: tests/map_test.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/map_test: $(CORE) $(BUILD_DIR)/map_test.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
map-test: $(BUILD_DIR)/map_test
	$(BUILD_DIR)/map_test
-include $(BUILD_DIR)/map_test.d
.PHONY: map-test

$(BUILD_DIR)/player_cli.o: tests/player_cli.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/player_cli: $(CORE) $(BUILD_DIR)/player_cli.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
player-process-test: $(BUILD_DIR)/player_cli
	python3 tests/player_process_test.py $(BUILD_DIR)/player_cli $(BUILD_DIR)/player_cli $(BUILD_DIR)/player-process-logs
-include $(BUILD_DIR)/player_cli.d
.PHONY: player-process-test

$(BUILD_DIR)/online_menu_test.o: tests/online_menu_test.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/online_menu_test: $(CORE) $(BUILD_DIR)/online_menu_test.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
online-test: $(BUILD_DIR)/online_menu_test
	$(BUILD_DIR)/online_menu_test
-include $(BUILD_DIR)/online_menu_test.d
.PHONY: online-test

# Local asynchronous asset scheduling remains header-only. Stage 3C adds three window components.
all: $(BUILD_DIR)/async_local_asset_test
test: asset-test
$(BUILD_DIR)/async_local_asset_test.o: tests/async_local_asset_test.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/async_local_asset_test: $(BUILD_DIR)/async_local_asset_test.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
asset-test: $(BUILD_DIR)/async_local_asset_test
	$(BUILD_DIR)/async_local_asset_test
-include $(BUILD_DIR)/async_local_asset_test.d
.PHONY: asset-test

all: $(BUILD_DIR)/window_test
test: windows-test
$(BUILD_DIR)/window_test.o: tests/window_test.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/window_test: $(CORE) $(BUILD_DIR)/window_test.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
windows-test: $(BUILD_DIR)/window_test
	$(BUILD_DIR)/window_test
-include $(BUILD_DIR)/window_test.d
.PHONY: windows-test

all: $(BUILD_DIR)/window_cli
$(BUILD_DIR)/window_cli.o: tests/window_cli.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/window_cli: $(CORE) $(BUILD_DIR)/window_cli.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
.PHONY: windows-process-test
windows-process-test: $(BUILD_DIR)/window_cli
	python3 tests/window_process_test.py $(BUILD_DIR)/window_cli $(BUILD_DIR)/window_cli $(BUILD_DIR)/window-process-logs

-include $(BUILD_DIR)/window_cli.d

all: $(BUILD_DIR)/video_test
test: video-test
$(BUILD_DIR)/video_test.o: tests/video_test.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/video_test: $(CORE) $(BUILD_DIR)/video_test.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
video-test: $(BUILD_DIR)/video_test
	$(BUILD_DIR)/video_test
-include $(BUILD_DIR)/video_test.d
.PHONY: video-test

# Explicit optional tests only. Default Core targets never query or link GStreamer.
MEDIA_PKGS = gstreamer-1.0 gstreamer-app-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 gstreamer-video-1.0
.PHONY: media-sdk media-test media-process-test readback-test
media-sdk:
	@pkg-config --atleast-version=1.22 gstreamer-1.0 && pkg-config --exists $(MEDIA_PKGS) || { echo 'GStreamer >=1.22 SDK missing; base make test needs no media SDK'; exit 1; }
$(BUILD_DIR)/MediaSession.o: media/gstreamer/MediaSession.cpp media/gstreamer/MediaSession.hpp | $(BUILD_DIR) media-sdk
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-misleading-indentation $$(pkg-config --cflags $(MEDIA_PKGS)) -c $< -o $@
$(BUILD_DIR)/media_cli.o: tests/media_cli.cpp | $(BUILD_DIR) media-sdk
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-misleading-indentation -c $< -o $@
$(BUILD_DIR)/media_probe.o: tests/media_probe.cpp | $(BUILD_DIR) media-sdk
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/media_cli: $(CORE) $(BUILD_DIR)/MediaSession.o $(BUILD_DIR)/media_cli.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) $$(pkg-config --libs $(MEDIA_PKGS)) -o $@
$(BUILD_DIR)/media_probe: $(CORE) $(BUILD_DIR)/MediaSession.o $(BUILD_DIR)/media_probe.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) $$(pkg-config --libs $(MEDIA_PKGS)) -o $@
$(BUILD_DIR)/media_codec_test: tests/media_codec_test.cpp | $(BUILD_DIR) media-sdk
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-range-loop-construct -Wno-misleading-indentation $$(pkg-config --cflags $(MEDIA_PKGS)) $< $(LDFLAGS) $(LDLIBS) $$(pkg-config --libs $(MEDIA_PKGS)) -o $@
media-test: $(BUILD_DIR)/media_probe $(BUILD_DIR)/media_codec_test
	$(BUILD_DIR)/media_probe
	$(BUILD_DIR)/media_codec_test
media-process-test: $(BUILD_DIR)/media_cli
	python3 tests/media_process_test.py $(BUILD_DIR)/media_cli $(BUILD_DIR)/media_cli $(BUILD_DIR)/media-process-logs
$(BUILD_DIR)/readback_egl_test: media/gl/Readback.cpp tests/readback_egl_test.cpp $(CORE) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-misleading-indentation $$(pkg-config --cflags egl glesv2) $^ $(LDFLAGS) $(LDLIBS) $$(pkg-config --libs egl glesv2) -o $@
readback-test: $(BUILD_DIR)/readback_egl_test
	$(BUILD_DIR)/readback_egl_test
-include $(BUILD_DIR)/MediaSession.d $(BUILD_DIR)/media_cli.d $(BUILD_DIR)/media_probe.d

.PHONY: media-lifetime-test
$(BUILD_DIR)/media_lifetime_test: tests/media_lifetime_test.cpp media/gstreamer/MediaSession.cpp media/gstreamer/MediaSession.hpp $(CORE) | $(BUILD_DIR) media-sdk
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-misleading-indentation $$(pkg-config --cflags $(MEDIA_PKGS)) tests/media_lifetime_test.cpp $(CORE) $(LDFLAGS) $(LDLIBS) $$(pkg-config --libs $(MEDIA_PKGS)) -o $@
media-lifetime-test: $(BUILD_DIR)/media_lifetime_test
	$(BUILD_DIR)/media_lifetime_test

# Hotfix 2 ICE policy regressions: pure C++, no media SDK or runtime required.
all: $(BUILD_DIR)/ice_test
test: ice-test
$(BUILD_DIR)/ice_test.o: tests/ice_test.cpp media/gstreamer/IceSignaling.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD_DIR)/ice_test: $(CORE) $(BUILD_DIR)/ice_test.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
ice-test: $(BUILD_DIR)/ice_test
	$(BUILD_DIR)/ice_test
-include $(BUILD_DIR)/ice_test.d
.PHONY: ice-test

$(BUILD_DIR)/MediaSession.o: media/gstreamer/IceSignaling.hpp
$(BUILD_DIR)/media_lifetime_test: media/gstreamer/IceSignaling.hpp
$(BUILD_DIR)/media_ice_test: tests/media_ice_test.cpp media/gstreamer/MediaSession.cpp media/gstreamer/MediaSession.hpp media/gstreamer/IceSignaling.hpp $(CORE) | $(BUILD_DIR) media-sdk
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-misleading-indentation $$(pkg-config --cflags $(MEDIA_PKGS)) tests/media_ice_test.cpp $(CORE) $(LDFLAGS) $(LDLIBS) $$(pkg-config --libs $(MEDIA_PKGS)) -o $@
media-ice-test: $(BUILD_DIR)/media_ice_test
	$(BUILD_DIR)/media_ice_test
.PHONY: media-ice-test

$(BUILD_DIR)/media_ice_cli.o: tests/media_ice_cli.cpp | $(BUILD_DIR) media-sdk
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-misleading-indentation -c $< -o $@
$(BUILD_DIR)/media_ice_cli: $(CORE) $(BUILD_DIR)/MediaSession.o $(BUILD_DIR)/media_ice_cli.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) $$(pkg-config --libs $(MEDIA_PKGS)) -o $@
media-ice-process-test: $(BUILD_DIR)/media_ice_cli
	python3 tests/media_ice_process_test.py $(BUILD_DIR)/media_ice_cli $(BUILD_DIR)/media_ice_cli $(BUILD_DIR)/media-ice-process-logs
-include $(BUILD_DIR)/media_ice_cli.d
.PHONY: media-ice-process-test

# Hotfix 3 pure driver scheduling tests; NO GStreamer dependency in default tests.
all: $(BUILD_DIR)/media_driver_test
test: media-driver-test
$(BUILD_DIR)/media_driver_test: tests/media_driver_test.cpp tests/MediaTestControl.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/media_driver_test.cpp $(LDFLAGS) $(LDLIBS) -o $@
media-driver-test: $(BUILD_DIR)/media_driver_test
	$(BUILD_DIR)/media_driver_test

# Exact production admission methods with a stopped worker and permit-only fixtures.
# This is NOT the live ICE/DTLS/VP8 test.
$(BUILD_DIR)/media_admission_test: tests/media_admission_test.cpp media/gstreamer/MediaSession.cpp media/gstreamer/MediaSession.hpp media/gstreamer/IceSignaling.hpp $(CORE) | $(BUILD_DIR) media-sdk
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Wno-misleading-indentation $$(pkg-config --cflags $(MEDIA_PKGS)) tests/media_admission_test.cpp $(CORE) $(LDFLAGS) $(LDLIBS) $$(pkg-config --libs $(MEDIA_PKGS)) -o $@
media-admission-test: $(BUILD_DIR)/media_admission_test
	$(BUILD_DIR)/media_admission_test
.PHONY: media-driver-test media-admission-test

$(BUILD_DIR)/media_cli.o $(BUILD_DIR)/media_ice_cli.o: tests/MediaScenarioDriver.hpp tests/MediaTestControl.hpp
-include $(BUILD_DIR)/media_driver_test.d

# Test the Python supervisor's verdict handling only, never substitute media.
media-driver-process-test:
	python3 tests/media_process_driver_test.py
.PHONY: media-driver-process-test

