.PHONY: format format-check format-diff test test-provisioning-form test-wifi-import test-connectivity-policy test-wifi-epoch-trace help install-hooks

# Use clang-format-18 for consistency with CI
# On macOS: brew install llvm@18 && brew link llvm@18
# On Ubuntu: sudo apt-get install clang-format-18
CLANG_FORMAT := $(shell which clang-format-18 2>/dev/null || which /opt/homebrew/opt/llvm@18/bin/clang-format 2>/dev/null || echo clang-format)

# Find all C and H files in main/ and components/ directories
# Exclude build/, managed_components/, etc.
C_FILES := $(shell find main -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) 2>/dev/null) \
	   $(shell find components -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) 2>/dev/null) \
	   $(shell find host_tests -type f -not -path "host_tests/build/*" \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) 2>/dev/null)

# Find all Python files in the project root and docs/
PY_FILES := $(shell find main -type f -name "*.py" 2>/dev/null) \
	    $(shell find scripts -type f -name "*.py" 3>/dev/null)

help:
	@echo "Available targets:"
	@echo "  format        - Format all C/H/Python and JS/Vue files"
	@echo "  format-check  - Check if files need formatting (non-zero exit if changes needed)"
	@echo "  format-diff   - Show what would change without modifying files"
	@echo "  test          - Build and run unit tests (requires ESP-IDF environment)"
	@echo "  test-provisioning-form - Run dependency-free provisioning boundary tests"
	@echo "  test-wifi-import - Run dependency-free wifi.txt import tests"
	@echo "  test-connectivity-policy - Run dependency-free boot/retry policy tests"
	@echo "  test-wifi-epoch-trace - Run dependency-free WiFi epoch trace checker tests"
	@echo "  install-hooks - Enable the git pre-commit formatting hook (.githooks)"

install-hooks:
	@git config core.hooksPath .githooks
	@echo "Git hooks enabled (core.hooksPath = .githooks)."
	@echo "Commits now run 'make format-check' first."

format:
	@echo "Formatting C/H files..."
	@$(CLANG_FORMAT) -i $(C_FILES)
	@echo "Done! Formatted $(words $(C_FILES)) C/H files."
	@if [ -n "$(PY_FILES)" ]; then \
		echo "Formatting Python files with isort..."; \
		python3 -m isort $(PY_FILES); \
		echo "Formatting Python files with black..."; \
		python3 -m black $(PY_FILES); \
		echo "Done! Formatted $(words $(PY_FILES)) Python files."; \
	fi
	@cd webapp && npm run format
	@echo "Done! Formatted webapp files."
	@echo "Formatting process-cli JS files..."
	@cd process-cli && npm run format
	@echo "Done! Formatted process-cli files."

format-check:
	@echo "Checking C/H files formatting..."
	@$(CLANG_FORMAT) --dry-run --Werror $(C_FILES)
	@if [ -n "$(PY_FILES)" ]; then \
		echo "Checking Python files formatting..."; \
		python3 -m isort --check-only $(PY_FILES); \
		python3 -m black --check $(PY_FILES); \
	fi
	@echo "Checking webapp Vue files formatting..."
	@cd webapp && [ -d node_modules ] || npm ci
	@cd webapp && npm run format:check
	@echo "Checking process-cli JS files formatting..."
	@cd process-cli && [ -d node_modules ] || npm ci
	@cd process-cli && npm run format:check
	@echo "All files are properly formatted!"

format-diff:
	@echo "Showing formatting differences for C/H files..."
	@for file in $(C_FILES); do \
		echo "=== $$file ==="; \
		$(CLANG_FORMAT) "$$file" | diff -u "$$file" - || true; \
	done
	@if [ -n "$(PY_FILES)" ]; then \
		echo "Showing formatting differences for Python files..."; \
		for file in $(PY_FILES); do \
			echo "=== $$file ==="; \
			python3 -m black --diff "$$file" || true; \
		done; \
	fi

test:
	@$(MAKE) test-provisioning-form
	@$(MAKE) test-wifi-import
	@$(MAKE) test-connectivity-policy
	@$(MAKE) test-wifi-epoch-trace
	@echo "Building and running host-based unit tests..."
	@mkdir -p host_tests/build
	@cd host_tests/build && cmake .. && make
	@echo ""
	@echo "Running C unit tests..."
	@./host_tests/build/cron_test
	@echo ""
	@echo "Running image orientation tests..."
	@cd process-cli && npm install --silent && npm run test:orientation
	@echo ""
	@echo "✓ All tests passed!"

test-provisioning-form:
	@mkdir -p host_tests/build
	@$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -Imain \
		main/provisioning_form.c host_tests/test_provisioning_form.c \
		-o host_tests/build/provisioning_form_test
	@./host_tests/build/provisioning_form_test

test-wifi-import:
	@mkdir -p host_tests/build
	@$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -Imain \
		main/wifi_import.c host_tests/test_wifi_import.c \
		-o host_tests/build/wifi_import_test
	@./host_tests/build/wifi_import_test

test-connectivity-policy:
	@mkdir -p host_tests/build
	@$(CC) -std=c11 -Wall -Wextra -Werror -pedantic -Imain \
		main/connectivity_policy.c host_tests/test_connectivity_policy.c \
		-o host_tests/build/connectivity_policy_test
	@./host_tests/build/connectivity_policy_test

test-wifi-epoch-trace:
	@cd tools/wifi_epoch_fence_probe && python3 -m unittest -v test_checker.py
	@python3 tools/wifi_epoch_fence_probe/check_trace.py \
		tools/wifi_epoch_fence_probe/traces/pass_scenario_a.jsonl
