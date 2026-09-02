CROSS_COMPILE ?= aarch64-linux-gnu-
CXX := $(CROSS_COMPILE)g++

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
LEARNING_ROOT := $(abspath $(ROOT)/..)
VENDOR := $(ROOT)/third_party/librga
FILE_SOURCE := $(ROOT)/src/rga_nv12_resize.cpp
LIVE_SOURCE := $(ROOT)/src/rga_v4l2_live.cpp
HEADERS := $(wildcard $(VENDOR)/include/*.h) \
	$(wildcard $(VENDOR)/include/*.hpp)
VENDOR_LIB := $(VENDOR)/lib/aarch64/librga.so

BUILD := $(ROOT)/build/rga_nv12_resize
FILE_BIN := $(BUILD)/bin/rga_nv12_resize
LIVE_BIN := $(BUILD)/bin/rga_v4l2_live
LIB := $(BUILD)/lib/librga.so

CPPFLAGS += -I$(VENDOR)/include -I$(LEARNING_ROOT)/benchmarks/include
CXXFLAGS += -O2 -g -Wall -Wextra -Werror -std=gnu++17
LDFLAGS += -L$(VENDOR)/lib/aarch64
LDFLAGS += -Wl,--enable-new-dtags,-rpath,'$$ORIGIN/../lib'
LDLIBS += -lrga -pthread

.PHONY: all bundle clean

all: bundle

bundle: $(FILE_BIN) $(LIVE_BIN) $(LIB)

$(FILE_BIN): $(FILE_SOURCE) $(HEADERS) $(VENDOR_LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< \
		$(LDFLAGS) $(LDLIBS) -o $@

$(LIVE_BIN): $(LIVE_SOURCE) $(HEADERS) $(VENDOR_LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< \
		$(LDFLAGS) $(LDLIBS) -o $@

$(LIB): $(VENDOR_LIB)
	@mkdir -p $(dir $@)
	cp $< $@

clean:
	rm -rf $(ROOT)/build
