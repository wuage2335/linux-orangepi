CC ?= gcc
CROSS_COMPILE ?= aarch64-linux-gnu-

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
KERNEL_ROOT := $(abspath $(ROOT)/../..)
BUILD := $(ROOT)/build
UAPI := $(BUILD)/uapi
HOST := $(BUILD)/host
ARM := $(BUILD)/aarch64

PROBE_SOURCE := $(ROOT)/src/rkmodule_info_probe.c
SHIM_SOURCE := $(ROOT)/src/rkmodule_info_preload.c
HOST_PROBE := $(HOST)/rkmodule_info_probe
HOST_SHIM := $(HOST)/librkmodule_info_preload.so
ARM_PROBE := $(ARM)/rkmodule_info_probe
ARM_SHIM := $(ARM)/librkmodule_info_preload.so

CFLAGS := -O2 -g -Wall -Wextra -Werror -I$(UAPI)

.PHONY: host-tools aarch64-tools test clean

$(UAPI)/linux/rk-camera-module.h: $(KERNEL_ROOT)/include/uapi/linux/rk-camera-module.h
	@mkdir -p $(dir $@)
	cp $< $@

$(UAPI)/linux/rk-video-format.h: $(KERNEL_ROOT)/include/uapi/linux/rk-video-format.h
	@mkdir -p $(dir $@)
	cp $< $@

$(HOST_PROBE): $(PROBE_SOURCE) $(UAPI)/linux/rk-camera-module.h $(UAPI)/linux/rk-video-format.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

$(HOST_SHIM): $(SHIM_SOURCE) $(UAPI)/linux/rk-camera-module.h $(UAPI)/linux/rk-video-format.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fPIC -shared $< -ldl -o $@

$(ARM_PROBE): $(PROBE_SOURCE) $(UAPI)/linux/rk-camera-module.h $(UAPI)/linux/rk-video-format.h
	@mkdir -p $(dir $@)
	$(CROSS_COMPILE)gcc $(CFLAGS) $< -o $@

$(ARM_SHIM): $(SHIM_SOURCE) $(UAPI)/linux/rk-camera-module.h $(UAPI)/linux/rk-video-format.h
	@mkdir -p $(dir $@)
	$(CROSS_COMPILE)gcc $(CFLAGS) -fPIC -shared $< -ldl -o $@

host-tools: $(HOST_PROBE) $(HOST_SHIM)

aarch64-tools: $(ARM_PROBE) $(ARM_SHIM)

test: host-tools
	bash $(ROOT)/tests/test_module_info_preload.sh $(ROOT)
	bash $(ROOT)/tests/test_capture_image_stats.sh $(ROOT)
	bash $(ROOT)/tests/test_fetch_build_rkaiq.sh $(ROOT)

clean:
	rm -rf $(HOST) $(ARM) $(UAPI)
