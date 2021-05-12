/*
 * Copyright (c) 2018 Xilinx Inc.
 * Written by Francisco Iglesias
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <sstream>
#include <string>
#include <sstream>
#include <vector>
#include <array>

#include <stdio.h>
#include <stdlib.h>

#define SC_INCLUDE_DYNAMIC_PROCESSES

#include "systemc"
using namespace sc_core;
using namespace sc_dt;
using namespace std;

#include "tlm.h"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"

#include "tlm-modules/tlm-wrap-expander.h"
#include "traffic-generators/tg-tlm.h"
#include "traffic-generators/traffic-desc.h"
#include "test-modules/memory.h"
#include "test-modules/utils.h"

using namespace utils;

#define GENATTR_WRAP(wrap) \
GenAttr(0, false, false, wrap)

DataTransferVec transactions = {
	//
	// Non wrap accesses
	//
        Write(0, DATA(0x1, 0x2, 0x3, 0x4)),
        Read(0),
		Expect(DATA(0x1, 0x2, 0x3, 0x4), 4),

        Write(0, DATA(0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
		      0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
		      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f), 32),
        Read(0, 32),
		Expect(DATA(
			0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f), 32),

	//
	// Test reads
	//

	// Start at 4
        Read(4, 32),
		GENATTR_WRAP(true),
		Expect(DATA(
			0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc,
			0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14,
			0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
			0x1d, 0x1e, 0x1f, 0x0, 0x1, 0x2, 0x3, ), 32),

	// Start at 16
        Read(16, 32),
		GENATTR_WRAP(true),
		Expect(DATA(
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf), 32),

	// Start at 24
        Read(24, 32),
		GENATTR_WRAP(true),
		Expect(DATA(
			0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17), 32),

	//
	// Test writes
	//

	// Start at 8
        Write(8, DATA(0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
		      0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
		      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f), 32),
		GENATTR_WRAP(true),
        Read(0, 32),
		Expect(DATA(
			0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17), 32),

	// Start at 16
        Write(16, DATA(0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
		      0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
		      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f), 32),
		GENATTR_WRAP(true),
        Read(0, 32),
		Expect(DATA(
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf), 32),

	// Start at 28
        Write(28, DATA(0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
		      0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
		      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f), 32),
		GENATTR_WRAP(true),
        Read(0, 32),
		Expect(DATA(
			0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc,
			0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14,
			0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
			0x1d, 0x1e, 0x1f, 0x0, 0x1, 0x2, 0x3, ), 32),

	//
	// Test with byte enable
	//

	// Start at 8
        Write(8, DATA(0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
		      0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
		      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f), 32),
		GENATTR_WRAP(true),
		ByteEnable(DATA(
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff), 32),
        Read(0, 32),
		Expect(DATA(
			0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17), 32),
	// Start at 16
        Write(16, DATA(0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
		      0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
		      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f), 32),
		GENATTR_WRAP(true),
		ByteEnable(DATA(
			0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff), 32),
        Read(0, 32),
		Expect(DATA(
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
			0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf), 32),

};

SC_MODULE(Dut)
{
public:
	enum { RamSize = 256 * 1024 };

	Dut(sc_module_name name, DataTransferVec &transfers) :
		tg("tg"),
		wrap_expander("wrap_expander", true), // generate two gps
		ram("ram", sc_time(1, SC_NS), RamSize),
		xfers(merge(transfers))
	{
		tg.enableDebug();
		tg.addTransfers(xfers, 0);

		// tg -> exmon -> ram
		tg.socket.bind(wrap_expander.target_socket);
		wrap_expander.init_socket.bind(ram.socket);
	}

private:
	TLMTrafficGenerator tg;
	tlm_wrap_expander wrap_expander;
	memory ram;
	TrafficDesc xfers;
};

SC_MODULE(Top)
{
	Dut dut;

	Top(sc_module_name name,
	    DataTransferVec &transfers_dut) :
		dut("dut", transfers_dut)
	{ }
};

int sc_main(int argc, char *argv[])
{
	Top top("Top", transactions);

	sc_trace_file *trace_fp = sc_create_vcd_trace_file(argv[0]);
	sc_start(100, SC_MS);
	sc_stop();

	if (trace_fp) {
		sc_close_vcd_trace_file(trace_fp);
	}
	return 0;
}
