# PlasmaZones Build System
# Simple Makefile wrapper for CMake
#
# Usage:
#   make              - Configure and build (Debug)
#   make release      - Configure and build (Release)
#   make install      - Install to system (requires sudo)
#   make uninstall    - Uninstall from system (requires sudo)
#   make clean        - Remove build directory
#   make test         - Build and run tests
#   make editor       - Build only the editor
#   make daemon       - Build only the daemon
#   make run-editor   - Build and run the editor
#   make run-daemon   - Build and run the daemon

# Configuration
BUILD_DIR := build
BUILD_TYPE ?= Debug
JOBS ?= $(shell nproc 2>/dev/null || echo 4)
CMAKE_FLAGS ?=

# Auto-detect color support
ifneq ($(TERM),)
  ifneq ($(TERM),dumb)
    BLUE := $(shell printf '\033[1;34m')
    GREEN := $(shell printf '\033[1;32m')
    YELLOW := $(shell printf '\033[1;33m')
    RED := $(shell printf '\033[1;31m')
    NC := $(shell printf '\033[0m')
  endif
endif

.PHONY: all configure build release install post-install uninstall clean test \
        editor daemon run-editor run-daemon help format format-cpp format-qml

# Default target
all: build

# Configure the build
configure:
	@echo "$(BLUE)>>> Configuring $(BUILD_TYPE) build...$(NC)"
	@cmake -B $(BUILD_DIR) -S . \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DBUILD_TESTING=ON \
		$(CMAKE_FLAGS)
	@echo "$(GREEN)>>> Configuration complete$(NC)"

# Build everything
build: configure
	@echo "$(BLUE)>>> Building with $(JOBS) jobs...$(NC)"
	@cmake --build $(BUILD_DIR) -j$(JOBS)
	@echo "$(GREEN)>>> Build complete$(NC)"
	@echo "Binaries in: $(BUILD_DIR)/bin/"

# Release build
release:
	@$(MAKE) BUILD_TYPE=Release build

# Install to system (CMake install prints success message, runs sycoca via script)
# Sycoca runs as original user when using sudo (handled by cmake/post-install-sycoca.sh)
install: build
	@echo "$(YELLOW)>>> Installing (may require sudo)...$(NC)"
	@cmake --install $(BUILD_DIR)
	@echo "$(GREEN)>>> Installation complete$(NC)"

# Refresh KDE service cache (standalone, e.g. after ninja install or if KCM not visible)
post-install:
	@echo "$(BLUE)>>> Refreshing KDE service cache...$(NC)"
	@$(SHELL) cmake/post-install-sycoca.sh
	@echo "$(GREEN)>>> Done. PlasmaZones should now appear in System Settings.$(NC)"

# Uninstall from system (uses full CMake uninstall: manifest + all known /usr paths)
uninstall:
	@if [ -f $(BUILD_DIR)/cmake_uninstall.cmake ]; then \
		echo "$(YELLOW)>>> Uninstalling (may require sudo)...$(NC)"; \
		cmake --build $(BUILD_DIR) --target uninstall; \
		echo "$(GREEN)>>> Uninstall complete$(NC)"; \
	elif [ -f $(BUILD_DIR)/install_manifest.txt ]; then \
		echo "$(YELLOW)>>> Uninstalling via manifest only (may require sudo)...$(NC)"; \
		xargs rm -vf < $(BUILD_DIR)/install_manifest.txt; \
		echo "$(GREEN)>>> Uninstall complete$(NC)"; \
	else \
		echo "$(RED)>>> No build found. Run 'make' and 'make install' first.$(NC)"; \
		exit 1; \
	fi

# Clean build directory
clean:
	@echo "$(YELLOW)>>> Cleaning build directory...$(NC)"
	@rm -rf $(BUILD_DIR)
	@echo "$(GREEN)>>> Clean complete$(NC)"

# Build and run tests
test: build
	@echo "$(BLUE)>>> Running tests...$(NC)"
	@cd $(BUILD_DIR) && ctest --output-on-failure
	@echo "$(GREEN)>>> Tests complete$(NC)"

# Build only the editor
editor: configure
	@echo "$(BLUE)>>> Building editor...$(NC)"
	@cmake --build $(BUILD_DIR) --target plasmazones-editor -j$(JOBS)
	@echo "$(GREEN)>>> Editor built: $(BUILD_DIR)/bin/plasmazones-editor$(NC)"

# Build only the daemon
daemon: configure
	@echo "$(BLUE)>>> Building daemon...$(NC)"
	@cmake --build $(BUILD_DIR) --target plasmazonesd -j$(JOBS)
	@echo "$(GREEN)>>> Daemon built: $(BUILD_DIR)/bin/plasmazonesd$(NC)"

# Build and run the editor
run-editor: editor
	@echo "$(BLUE)>>> Running editor...$(NC)"
	@$(BUILD_DIR)/bin/plasmazones-editor --new

# Build and run the daemon
run-daemon: daemon
	@echo "$(BLUE)>>> Running daemon...$(NC)"
	@$(BUILD_DIR)/bin/plasmazonesd

# Rebuild from scratch
rebuild: clean build

# Format C++ (clang-format) and QML (qmlformat, if available)
format: configure
	@echo "$(BLUE)>>> Formatting C++...$(NC)"
	@cmake --build $(BUILD_DIR) --target clang-format
	@echo "$(BLUE)>>> Formatting QML...$(NC)"
	@cmake --build $(BUILD_DIR) --target format-qml
	@echo "$(GREEN)>>> Format complete$(NC)"

# Format C++ only
format-cpp: configure
	@echo "$(BLUE)>>> Formatting C++...$(NC)"
	@cmake --build $(BUILD_DIR) --target clang-format
	@echo "$(GREEN)>>> C++ format complete$(NC)"

# Format QML only (requires qt6-tools: qmlformat)
format-qml: configure
	@echo "$(BLUE)>>> Formatting QML...$(NC)"
	@cmake --build $(BUILD_DIR) --target format-qml
	@echo "$(GREEN)>>> QML format complete$(NC)"

# Help
help:
	@echo "$(BLUE)PlasmaZones Build System$(NC)"
	@echo ""
	@echo "$(GREEN)Build targets:$(NC)"
	@echo "  make              - Configure and build (Debug)"
	@echo "  make release      - Configure and build (Release)"
	@echo "  make editor       - Build only the layout editor"
	@echo "  make daemon       - Build only the daemon"
	@echo "  make rebuild      - Clean and rebuild everything"
	@echo ""
	@echo "$(GREEN)Run targets:$(NC)"
	@echo "  make run-editor   - Build and run the editor"
	@echo "  make run-daemon   - Build and run the daemon"
	@echo ""
	@echo "$(GREEN)Install targets:$(NC)"
	@echo "  make install      - Install to system (may need sudo)"
	@echo "  make post-install - Refresh KDE cache (e.g. after ninja install)"
	@echo "  make uninstall    - Uninstall from system (may need sudo)"
	@echo ""
	@echo "$(GREEN)Other targets:$(NC)"
	@echo "  make test         - Build and run tests"
	@echo "  make format       - Format C++ and QML (clang-format, qmlformat)"
	@echo "  make format-cpp   - Format C++ only"
	@echo "  make format-qml   - Format QML only (needs qt6-tools)"
	@echo "  make clean        - Remove build directory"
	@echo "  make help         - Show this help"
	@echo ""
	@echo "$(GREEN)Variables:$(NC)"
	@echo "  BUILD_TYPE=Debug|Release  - Build type (default: Debug)"
	@echo "  JOBS=N                    - Parallel jobs (default: nproc)"
	@echo "  CMAKE_FLAGS='...'         - Extra CMake flags"
	@echo ""
	@echo "$(GREEN)Examples:$(NC)"
	@echo "  make                      - Debug build"
	@echo "  make release              - Release build"
	@echo "  make JOBS=4 editor        - Build editor with 4 jobs"
	@echo "  sudo make install         - Install to system"
