CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-deprecated-declarations -Isrc

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

CORE_SRCS := src/game_sim.cpp src/game_rules.cpp src/game_ai.cpp src/audio.cpp src/i18n.cpp src/font.cpp src/app.cpp src/render_table.cpp src/render_ui.cpp
TEST_SRCS := src/game_sim.cpp src/game_rules.cpp src/game_ai.cpp src/audio.cpp src/i18n.cpp src/font.cpp tests/test_game.cpp

BUILD_DIR := build
NOVUSS_BIN := $(BUILD_DIR)/novuss
TEST_BIN := $(BUILD_DIR)/test_game

.PHONY: all test ps3 clean

all: $(NOVUSS_BIN) $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(NOVUSS_BIN): $(CORE_SRCS) src/main_sdl.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(CORE_SRCS) src/main_sdl.cpp -o $@ $(SDL_LIBS)

$(TEST_BIN): $(TEST_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(TEST_SRCS) -o $@

test: $(TEST_BIN) $(NOVUSS_BIN)
	./$(TEST_BIN)
	./$(NOVUSS_BIN) --headless-test

ps3:
	docker run --rm -v "$(PWD)":/work -w /work zeldin/ps3dev-docker:arm64 make -f Makefile.ps3 pkg

clean:
	rm -rf $(BUILD_DIR)
