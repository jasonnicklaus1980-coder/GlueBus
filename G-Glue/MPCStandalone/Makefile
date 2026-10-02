# Cross-build of the shared engine + MPC bridge for MPC hardware CPUs, with the tests run under QEMU.
#   make -C MPCStandalone arm      32-bit ARM hard-float (armv7-a, NEON): original MPC X / Live / One / Force CPU
#   make -C MPCStandalone test     builds and runs DSP tests + bridge tests under qemu-arm
# Output: ../build/mpc-armv7/libgglue_mpc.a  (static library a native MPC plugin wrapper would link)
CROSS ?= arm-linux-gnueabihf-
QEMU  ?= qemu-arm-static
SYSROOT ?= /usr/arm-linux-gnueabihf
OUT   := ../build/mpc-armv7
FLAGS := -std=c++17 -O2 -Wall -Wextra -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -mthumb -fPIC -fvisibility=hidden -I..
CORE  := ../DSP/GGlueEngine.cpp ../Parameters/GGlueParameters.cpp ../Presets/GGluePresets.cpp ../Presets/FactoryPresets.cpp GGlueMPCBridge.cpp
OBJS  := $(patsubst %.cpp,$(OUT)/obj/%.o,$(notdir $(CORE)))

arm: $(OUT)/libgglue_mpc.a $(OUT)/gglue_dsp_tests $(OUT)/gglue_bridge_test
$(OUT)/obj/%.o: ../DSP/%.cpp ; @mkdir -p $(@D); $(CROSS)g++ $(FLAGS) -c $< -o $@
$(OUT)/obj/%.o: ../Parameters/%.cpp ; @mkdir -p $(@D); $(CROSS)g++ $(FLAGS) -c $< -o $@
$(OUT)/obj/%.o: ../Presets/%.cpp ; @mkdir -p $(@D); $(CROSS)g++ $(FLAGS) -c $< -o $@
$(OUT)/obj/%.o: %.cpp ; @mkdir -p $(@D); $(CROSS)g++ $(FLAGS) -c $< -o $@
$(OUT)/libgglue_mpc.a: $(OBJS)
	$(CROSS)ar rcs $@ $^
$(OUT)/gglue_dsp_tests: ../Tests/DspTests.cpp $(OUT)/libgglue_mpc.a
	$(CROSS)g++ $(FLAGS) $< -o $@ $(OUT)/libgglue_mpc.a -static
$(OUT)/gglue_bridge_test: BridgeTest.c $(OUT)/libgglue_mpc.a
	$(CROSS)gcc -O2 -std=c11 -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -c $< -o $(OUT)/obj/BridgeTest.o
	$(CROSS)g++ $(FLAGS) $(OUT)/obj/BridgeTest.o -o $@ $(OUT)/libgglue_mpc.a -static
test: arm
	GGLUE_EMULATED=1 $(QEMU) $(OUT)/gglue_dsp_tests $(OUT)/work
	rm -rf $(OUT)/bridge-presets && $(QEMU) $(OUT)/gglue_bridge_test $(OUT)/bridge-presets
clean:
	rm -rf $(OUT)
.PHONY: arm test clean
