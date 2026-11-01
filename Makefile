# Thin wrapper around CMake. `make` builds (if needed) and starts the app.
#   make                    build and run with the remembered directories
#   make run ARGS=~/notes   also add these directories
#   make build | test | clean
#   make release [BUMP=patch|minor|major|X.Y.Z]   bump, test, tag and push (release.sh)

BUILD ?= build
ARGS  ?=
BUMP  ?= patch

.PHONY: run build test clean release

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

release:
	./release.sh $(BUMP)
