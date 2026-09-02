CXX ?= g++

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
LEARNING_ROOT := $(abspath $(ROOT)/..)
MPP_ROOT := $(LEARNING_ROOT)/mpp
STREAMING_ROOT := $(LEARNING_ROOT)/streaming
MPP_BUNDLE ?= $(MPP_ROOT)/build/bundle/official-mpp
BUILD := $(ROOT)/build
BIN := $(BUILD)/pipeline_stage_benchmark
SOURCE := $(ROOT)/src/pipeline_stage_benchmark.cpp
GST_SOURCE := $(STREAMING_ROOT)/src/gst_rtp_sink.cpp
TEST_STATS := $(BUILD)/test_timing_stats
TEST_CONFIG := $(BUILD)/test_pipeline_config

CPPFLAGS += -I$(ROOT)/include -I$(MPP_ROOT)/src -I$(STREAMING_ROOT)/src
CPPFLAGS += -I$(MPP_BUNDLE)/include/rockchip
CXXFLAGS += -O2 -g -Wall -Wextra -Werror -std=gnu++17
LDFLAGS += -L$(MPP_BUNDLE)/lib
LDLIBS += -lrockchip_mpp -pthread -ldl
GST_MODULES := gstreamer-1.0 gstreamer-app-1.0

.PHONY: all test clean

all: $(BIN)

$(BIN): $(SOURCE) $(GST_SOURCE)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		$$(pkg-config --cflags $(GST_MODULES)) \
		$(SOURCE) $(GST_SOURCE) $(LDFLAGS) \
		$$(pkg-config --libs $(GST_MODULES)) $(LDLIBS) -o $@

$(TEST_STATS): $(ROOT)/tests/test_timing_stats.cpp $(ROOT)/include/timing_stats.hpp
	@mkdir -p $(dir $@)
	$(CXX) -I$(ROOT)/include $(CXXFLAGS) $< -o $@

$(TEST_CONFIG): $(ROOT)/tests/test_pipeline_config.cpp \
		$(ROOT)/include/pipeline_benchmark_config.hpp
	@mkdir -p $(dir $@)
	$(CXX) -I$(ROOT)/include $(CXXFLAGS) $< -o $@

test: $(TEST_STATS) $(TEST_CONFIG)
	$(TEST_STATS)
	$(TEST_CONFIG)

clean:
	rm -rf $(BUILD)
