# Host tests (no Arduino needed). Usage:
#   make -C test                 unit tests with AddressSanitizer + UndefinedBehaviorSanitizer
#   make -C test tsan            multi-threaded tests with ThreadSanitizer
#   make -C test compile-fail    checks that type errors are rejected at compile time
#   make -C test examples        runs the single-core examples against a small Arduino stand-in
#   make -C test check           everything above
#
# Example 07 additionally needs ReflectJSON and ReflectINI. They are expected next to this
# library (Arduino/libraries/ReflectJSON, .../ReflectINI) or can be set with REFLECT_LIBS=<dir>.

CXX ?= g++
SRC := ../src
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined
TSANFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -fsanitize=thread
INC := -I$(SRC)
BUILD := build
REFLECT_LIBS ?= ../..

UNIT := test_vars test_vars64 test_host test_features test_reentrancy test_results \
        test_reflectcore test_foreign test_errorhook test_zero
THREADS := test_threads test_results_threads
FAIL_DEFINES := FAIL_CALLBACK_TYPE FAIL_OTHER_SET FAIL_GET_ASSIGN FAIL_FN_TYPE FAIL_OLD_API \
                FAIL_TRYGET_TYPE FAIL_PLAIN_ENUM FAIL_POOL_TOO_SMALL FAIL_FAKE_VAR FAIL_ADD_NODEFAULT
HOST_EXAMPLES := 01_NoLock_Basic 02_NoLock_ReflectStruct
HEADERS := $(wildcard $(SRC)/*.h) test_check.h

all: unit

check: unit tsan compile-fail examples

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%: %.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INC) $< -o $@ -lpthread

unit: $(addprefix $(BUILD)/,$(UNIT))
	@for t in $(UNIT); do ./$(BUILD)/$$t || exit 1; done
	@# strict standard conformance and builds without RTTI/exceptions
	$(CXX) -std=c++17 -pedantic-errors -Wall -Wextra -fsyntax-only $(INC) test_zero.cpp
	$(CXX) -std=c++17 -pedantic-errors -Wall -Wextra -fsyntax-only $(INC) test_vars64.cpp
	$(CXX) -std=c++17 -Wall -Wextra -fno-rtti -fno-exceptions -fsyntax-only $(INC) test_host.cpp
	@echo "== unit tests passed"

tsan: | $(BUILD)
	@for t in $(THREADS); do \
	  $(CXX) $(TSANFLAGS) $(INC) $$t.cpp -o $(BUILD)/$$t -lpthread && ./$(BUILD)/$$t || exit 1; done
	@echo "== thread tests passed"

compile-fail:
	@for d in $(FAIL_DEFINES); do \
	  if $(CXX) -std=c++17 -fsyntax-only $(INC) -D$$d test_vars.cpp >/dev/null 2>&1; then \
	    echo "compile-fail: $$d compiled but must not"; exit 1; fi; done
	@if $(CXX) -std=c++17 -fsyntax-only $(INC) -DSTRICT_MUST_FAIL test_foreign.cpp >/dev/null 2>&1; then \
	  echo "compile-fail: STRICT_MUST_FAIL compiled but must not"; exit 1; fi
	@for d in DISCARD_SET DISCARD_UPDATE; do \
	  if $(CXX) -std=c++17 -Werror=unused-result -fsyntax-only $(INC) -D$$d test_nodiscard.cpp >/dev/null 2>&1; then \
	    echo "compile-fail: $$d compiled but must not"; exit 1; fi; done
	@$(CXX) -std=c++17 -Werror=unused-result -fsyntax-only $(INC) test_nodiscard.cpp
	@echo "== compile-fail checks passed"

examples: | $(BUILD)
	@for e in $(HOST_EXAMPLES); do \
	  printf '#include "Arduino.h"\n#include "../../examples/%s/%s.ino"\nint main(){ setup(); loop(); }\n' $$e $$e > $(BUILD)/example_$$e.cpp; \
	  $(CXX) $(CXXFLAGS) -DARDUINO $(INC) -Imock $(BUILD)/example_$$e.cpp -o $(BUILD)/example_$$e && \
	  ./$(BUILD)/example_$$e > $(BUILD)/example_$$e.txt || exit 1; done
	@e=07_INI_JSON_State_SameStruct; \
	if [ -f $(REFLECT_LIBS)/ReflectJSON/src/ReflectJSON.h ] && [ -f $(REFLECT_LIBS)/ReflectINI/src/ReflectINI.h ]; then \
	  printf '#include "Arduino.h"\n#include "../../examples/%s/%s.ino"\nint main(){ setup(); loop(); }\n' $$e $$e > $(BUILD)/example_$$e.cpp; \
	  $(CXX) $(CXXFLAGS) -DARDUINO $(INC) -I$(REFLECT_LIBS)/ReflectJSON/src -I$(REFLECT_LIBS)/ReflectINI/src -Imock \
	    $(BUILD)/example_$$e.cpp -o $(BUILD)/example_$$e && ./$(BUILD)/example_$$e > $(BUILD)/example_$$e.txt || exit 1; \
	else echo "   (example 07 skipped: ReflectJSON/ReflectINI not found in $(REFLECT_LIBS))"; fi
	@echo "== examples passed (output in $(BUILD)/example_*.txt)"

clean:
	rm -rf $(BUILD)

.PHONY: all check unit tsan compile-fail examples clean
