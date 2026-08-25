CXX ?= g++

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
LEARNING_ROOT := $(abspath $(ROOT)/..)
MPP_ROOT := $(LEARNING_ROOT)/mpp
MPP_SRC ?= $(MPP_ROOT)/src
MPP_BUNDLE ?= $(MPP_ROOT)/build/bundle/official-mpp
BUILD := $(ROOT)/build
BIN_DIR := $(BUILD)/bin
LIB_DIR := $(BUILD)/lib

RTP_BIN := $(BIN_DIR)/v4l2_mpp_rtp_sender
RTSP_BIN := $(BIN_DIR)/v4l2_mpp_rtsp_server
SMOKE_BIN := $(BIN_DIR)/check_streaming_build
RTP_SOURCES := \
	$(ROOT)/src/gst_rtp_sink.cpp \
	$(ROOT)/src/v4l2_mpp_rtp_sender.cpp
RTSP_SOURCES := \
	$(ROOT)/src/gst_rtsp_server.cpp \
	$(ROOT)/src/v4l2_mpp_rtsp_server.cpp
SHARED_HEADERS := \
	$(MPP_SRC)/encoded_packet_sink.hpp \
	$(MPP_SRC)/mpp_encoder_core.hpp \
	$(MPP_SRC)/v4l2_capture.hpp
SMOKE_SOURCE := $(ROOT)/tests/check_streaming_build.cpp

GST_MODULES := gstreamer-1.0 gstreamer-app-1.0
GST_RTSP_MODULES := $(GST_MODULES) gstreamer-rtsp-server-1.0
CPPFLAGS += -I$(MPP_BUNDLE)/include/rockchip -I$(MPP_SRC) -I$(ROOT)/src
CXXFLAGS += -O2 -g -Wall -Wextra -Werror -std=gnu++17
MPP_LDFLAGS := -L$(MPP_BUNDLE)/lib
RPATH := -Wl,--enable-new-dtags,-rpath,'$$ORIGIN/../lib'
MPP_LDLIBS := -lrockchip_mpp -pthread -ldl

.PHONY: all rtp rtsp smoke check-env runtime-lib clean

all: rtp

check-env:
	@command -v $(CXX) >/dev/null
	@command -v pkg-config >/dev/null
	@pkg-config --exists $(GST_MODULES)
	@test -f $(MPP_BUNDLE)/include/rockchip/rk_mpi.h
	@test -f $(MPP_BUNDLE)/lib/librockchip_mpp.so.0
	@echo STREAMING_BUILD_ENV=OK

runtime-lib:
	@mkdir -p $(LIB_DIR)
	cp -a $(MPP_BUNDLE)/lib/librockchip_mpp.so* $(LIB_DIR)/

$(SMOKE_BIN): $(SMOKE_SOURCE) | check-env runtime-lib
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		$$(pkg-config --cflags $(GST_MODULES)) \
		$< $(MPP_LDFLAGS) $(RPATH) \
		$$(pkg-config --libs $(GST_MODULES)) $(MPP_LDLIBS) -o $@

smoke: $(SMOKE_BIN)
	LD_LIBRARY_PATH=$(LIB_DIR) $(SMOKE_BIN)

$(RTP_BIN): $(RTP_SOURCES) $(SHARED_HEADERS) | check-env runtime-lib
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		$$(pkg-config --cflags $(GST_MODULES)) \
		$(RTP_SOURCES) $(MPP_LDFLAGS) $(RPATH) \
		$$(pkg-config --libs $(GST_MODULES)) $(MPP_LDLIBS) -o $@

rtp: $(RTP_BIN)

$(RTSP_BIN): $(RTSP_SOURCES) $(RTP_SOURCES) $(SHARED_HEADERS) | runtime-lib
	@command -v $(CXX) >/dev/null
	@pkg-config --exists $(GST_RTSP_MODULES)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
		$$(pkg-config --cflags $(GST_RTSP_MODULES)) \
		$(filter-out %/v4l2_mpp_rtp_sender.cpp,$(RTP_SOURCES)) \
		$(RTSP_SOURCES) $(MPP_LDFLAGS) $(RPATH) \
		$$(pkg-config --libs $(GST_RTSP_MODULES)) $(MPP_LDLIBS) -o $@

rtsp: $(RTSP_BIN)

clean:
	rm -rf $(BUILD)
