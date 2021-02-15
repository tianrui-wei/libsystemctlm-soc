#
# Copyright (c) 2018 Xilinx Inc.
# Written by Francisco Iglesias,
#            Edgar E. Iglesias.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

-include ../../.config.mk
include ../Rules.mk

VFLAGS=-Wno-fatal --pins-bv 2 -sc

VM_TRACE=1

ifeq "$(VM_TRACE)" "1"
VFLAGS += --trace
VERILATED_OBJS_COMMON += verilated_vcd_c.o
VERILATED_OBJS_COMMON += verilated_vcd_sc.o
CPPFLAGS += -DVM_TRACE=1
endif

CPPFLAGS += -I $(VOBJ_DIR)
CPPFLAGS += -I $(VERILATOR_ROOT)/include

CPPFLAGS += -I ../../ -I ../ -I .
CXXFLAGS += -Wno-strict-overflow -Wall -g

OPENPITON_DEV_V += system.v
OPENPITON_DEV_H += $(VOBJ_DIR)/Vsystem.h
OPENPITON_DEV_LIB += $(VOBJ_DIR)/Vsystem__ALL.a

OBJS_COMMON += ../test-modules/memory.o
VERILATED_OBJS_COMMON += $(OBJS_COMMON)
ALL_OBJS += $(VERILATED_OBJS_COMMON)

EXAMPLE_RTL_TRI_OBJS += example-rtl-tri.o $(VERILATED_O)
ALL_OBJS += $(EXAMPLE_RTL_TRI_OBJS)

TARGETS += example-rtl-tri

################################################################################

all: $(TARGETS)

## Dep generation ##
-include $(ALL_OBJS:.o=.d)

include $(VERILATOR_ROOT)/include/verilated.mk

$(OPENPITON_DEV_LIB): $(OPENPITON_DEV_V)
	$(VENV) $(VERILATOR) $(VFLAGS) --top-module $(^:.v=) $^
	$(MAKE) -C $(VOBJ_DIR) CXXFLAGS="$(CXXFLAGS)" -f Vsystem.mk
	$(MAKE) -C $(VOBJ_DIR) CXXFLAGS="$(CXXFLAGS)" -f Vsystem.mk verilated.o

$(EXAMPLE_RTL_TRI_OBJS): $(OPENPITON_DEV_LIB)

example-rtl-tri: $(EXAMPLE_RTL_TRI_OBJS) $(OPENPITON_DEV_LIB) $(VERILATED_OBJS_COMMON)
	$(LINK.cc) $^ $(LDLIBS) -o $@

clean:
	$(RM) $(ALL_OBJS) $(ALL_OBJS:.o=.d)
	$(RM) $(TARGETS:=.vcd)
	$(RM) -r $(VOBJ_DIR)
	$(RM) $(TARGETS)
