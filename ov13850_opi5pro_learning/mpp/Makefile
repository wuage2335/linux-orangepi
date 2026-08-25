CROSS_COMPILE ?= aarch64-linux-gnu-
CXX := $(CROSS_COMPILE)g++
HOSTCXX ?= g++

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SDK := $(ROOT)/build/sdk
APP_BUILD := $(ROOT)/build/app
BUNDLE := $(ROOT)/build/bundle/official-mpp
SOURCE := $(ROOT)/src/nv12_mpp_encoder.cpp
BIN := $(APP_BUILD)/nv12_mpp_encoder
LIVE_SOURCE := $(ROOT)/src/v4l2_mpp_encoder.cpp
LIVE_BIN := $(APP_BUILD)/v4l2_mpp_encoder
CORE_HEADER := $(ROOT)/src/mpp_encoder_core.hpp
PACKET_HEADER := $(ROOT)/src/encoded_packet_sink.hpp
V4L2_HEADER := $(ROOT)/src/v4l2_capture.hpp
HOST_BUILD := $(ROOT)/build/host
SINK_TEST_SOURCE := $(ROOT)/tests/test_encoded_packet_sink.cpp
SINK_TEST_BIN := $(HOST_BUILD)/test_encoded_packet_sink
V4L2_CONTRACT_SOURCE := $(ROOT)/tests/check_v4l2_capture_compile.cpp
V4L2_CONTRACT_STAMP := $(HOST_BUILD)/check_v4l2_capture_compile.stamp

CPPFLAGS += -I$(SDK)/include/rockchip
CXXFLAGS += -O2 -g -Wall -Wextra -Werror -std=gnu++17
LDFLAGS += -L$(SDK)/lib
LDFLAGS += -Wl,--enable-new-dtags,-rpath,'$$ORIGIN/../lib'
LDLIBS += -lrockchip_mpp -pthread -ldl

.PHONY: all bundle clean sdk test

all: bundle

sdk:
	$(ROOT)/scripts/fetch_build_mpp.sh

$(BIN): $(SOURCE) $(CORE_HEADER) $(PACKET_HEADER) | sdk
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

$(LIVE_BIN): $(LIVE_SOURCE) $(CORE_HEADER) $(PACKET_HEADER) $(V4L2_HEADER) | sdk
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

bundle: $(BIN) $(LIVE_BIN)
	$(ROOT)/scripts/package_mpp_bundle.sh

$(SINK_TEST_BIN): $(SINK_TEST_SOURCE) $(PACKET_HEADER)
	@mkdir -p $(dir $@)
	$(HOSTCXX) -O2 -g -Wall -Wextra -Werror -std=gnu++17 $< -o $@

$(V4L2_CONTRACT_STAMP): $(V4L2_CONTRACT_SOURCE) $(V4L2_HEADER) | sdk
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -fsyntax-only $<
	@touch $@

test: $(SINK_TEST_BIN) $(V4L2_CONTRACT_STAMP)
	$(SINK_TEST_BIN)

clean:
	rm -rf $(APP_BUILD) $(HOST_BUILD) $(ROOT)/build/bundle
