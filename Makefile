# Thin wrapper around CMake. `make` builds (if needed) and starts the app.
#   make                    build and run with the remembered directories
#   make run ARGS=~/notes   also add these directories
#   make build | test | clean

BUILD ?= build
ARGS  ?=

.PHONY: run build test clean

run: build
	./$(BUILD)/todomd $(ARGS)

build: $(BUILD)/build.ninja
	cmake --build $(BUILD)

$(BUILD)/build.ninja:
	cmake -S . -B $(BUILD) -G Ninja

test: build
	cd $(BUILD) && ctest --output-on-failure

clean:
	rm -rf $(BUILD)
