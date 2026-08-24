CROSS_COMPILE ?= aarch64-linux-gnu-
CXX := $(CROSS_COMPILE)g++

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SDK := $(ROOT)/build/sdk
APP_BUILD := $(ROOT)/build/app
BUNDLE := $(ROOT)/build/bundle/official-mpp
SOURCE := $(ROOT)/src/nv12_mpp_encoder.cpp
BIN := $(APP_BUILD)/nv12_mpp_encoder

CPPFLAGS += -I$(SDK)/include/rockchip
CXXFLAGS += -O2 -g -Wall -Wextra -Werror -std=gnu++17
LDFLAGS += -L$(SDK)/lib
LDFLAGS += -Wl,--enable-new-dtags,-rpath,'$$ORIGIN/../lib'
LDLIBS += -lrockchip_mpp -pthread -ldl

.PHONY: all bundle clean sdk

all: bundle

sdk:
	$(ROOT)/scripts/fetch_build_mpp.sh

$(BIN): $(SOURCE) | sdk
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

bundle: $(BIN)
	$(ROOT)/scripts/package_mpp_bundle.sh

clean:
	rm -rf $(APP_BUILD) $(ROOT)/build/bundle
