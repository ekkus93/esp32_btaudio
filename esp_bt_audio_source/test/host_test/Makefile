# Makefile for standalone host tests (CI parity check)
#
# This builds host tests exactly as GitHub Actions CI does:
# - Clean build from scratch
# - Detects missing mock implementations
# - Catches linker errors that incremental builds might miss
#
# Usage:
#   make test      # Clean build + run tests
#   make build     # Clean build only
#   make clean     # Remove build directory

.PHONY: test build clean help

# Default target
test: clean build run

build:
	@echo "==> Building standalone host tests (CI parity)..."
	@mkdir -p build_host_tests
	@cd build_host_tests && cmake .. && cmake --build . -- -j$$(nproc)
	@echo "✅ Build successful"

run:
	@echo "==> Running tests..."
	@cd build_host_tests && ctest --output-on-failure
	@echo "✅ Tests passed"

clean:
	@echo "==> Cleaning build directory..."
	@rm -rf build_host_tests
	@echo "✅ Clean complete"

help:
	@echo "Standalone host test Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  test    - Clean build and run tests (default)"
	@echo "  build   - Clean build only"
	@echo "  run     - Run tests (requires build)"
	@echo "  clean   - Remove build directory"
	@echo "  help    - Show this help"
	@echo ""
	@echo "This matches the GitHub Actions CI workflow exactly."
