# make          native viewer (build/sprig) + web module (web/sprig.wasm)
# make test     core + CLI under ASan/UBSan, wasm under Node
# make install  copy sprig to $(PREFIX)/bin

CLANG    ?= clang++
PREFIX   ?= $(HOME)/.local
CXXFLAGS ?= -O2
WARN     := -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Werror
SAN_CXX  ?= $(CLANG)
SANITIZE := -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all
# wasm-ld ships with lld; Rust's toolchain carries a copy too.
WASM_LD  ?= $(firstword $(shell command -v wasm-ld 2>/dev/null) \
              $(wildcard $(HOME)/.rustup/toolchains/*/lib/rustlib/*/bin/gcc-ld/wasm-ld))

CORE := core/sprig.hpp
APP  := app/sprig.cpp app/events.hpp $(CORE)

all: build/sprig web/sprig.wasm

build:
	mkdir -p build

build/sprig: $(APP) | build
	$(CXX) $(WARN) $(CXXFLAGS) -o $@ app/sprig.cpp

build/sprig-asan: $(APP) | build
	$(SAN_CXX) $(WARN) $(SANITIZE) -o $@ app/sprig.cpp

build/core_test: tests/core_test.cpp app/events.hpp $(CORE) | build
	$(SAN_CXX) $(WARN) $(SANITIZE) -o $@ tests/core_test.cpp

web/sprig.wasm: core/sprig_wasm.cpp $(CORE) | build
	@test -n "$(WASM_LD)" || { echo "need wasm-ld: install lld, or set WASM_LD"; exit 1; }
	$(CLANG) --target=wasm32 $(WARN) -O2 -ffreestanding -fno-exceptions -fno-rtti -nostdlib \
	  -c -o build/sprig_wasm.o core/sprig_wasm.cpp
	$(WASM_LD) --no-entry --stack-first -z stack-size=65536 --gc-sections --strip-all \
	  -o $@ build/sprig_wasm.o

test: build/core_test build/sprig-asan web/sprig.wasm
	./build/core_test
	sh tests/cli_test.sh build/sprig-asan
	node tests/wasm_test.mjs

install: build/sprig
	install -Dm755 build/sprig $(PREFIX)/bin/sprig

clean:
	rm -rf build

.PHONY: all test install clean
