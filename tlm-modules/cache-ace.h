/*
 * Copyright (c) 2019 Xilinx Inc.
 * Written by Francisco Iglesias.
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
 * References:
 *
 *
 * [1] AMBA AXI and ACE Protocol Specification, ARM IHI 0022D, ID102711
 *
 */

#ifndef TLM_CACHE_ACE_H__
#define TLM_CACHE_ACE_H__

#include <list>

#include "tlm.h"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"
#include "tlm-extensions/genattr.h"
#include "tlm-bridges/amba.h"

using namespace AMBA::ACE;

enum WritePolicy { WriteBack, WriteThrough };

template<int CACHE_SZ, int CACHELINE_SZ = 64>
class cache_ace :
	public sc_core::sc_module
{
public:
	tlm_utils::simple_target_socket<cache_ace> target_socket;

	// Downstream
	tlm_utils::simple_initiator_socket<cache_ace> init_socket;
	tlm_utils::simple_target_socket<cache_ace> snoop_target_socket;

	cache_ace(sc_core::sc_module_name name,
			WritePolicy write_policy = WriteBack,
			uint64_t non_shareable_start = 0,
			unsigned int non_shareable_len = 0) :
		sc_core::sc_module(name),
		m_nonshareable_start(non_shareable_start),
		m_nonshareable_len(non_shareable_len),
		m_genattr(new genattr_extension()),
		m_cache(NULL)
	{
		if (write_policy == WriteBack) {
			m_cache = new ACECacheWriteBack(init_socket);
		} else {
			m_cache = new ACECacheWriteThrough(init_socket);
		}

		target_socket.register_b_transport(this, &cache_ace::b_transport);
		snoop_target_socket.register_b_transport(this, &cache_ace::b_transport_snoop);

		init_dvm_complete_gp();
	}

private:
	class IACECache
	{
	public:
		enum { NUM_CACHELINES = CACHE_SZ / CACHELINE_SZ };

		struct CacheLine
		{
			CacheLine() :
				valid(false),
				tag(0),
				shared(false),
				dirty(false)
			{}

			bool valid;
			uint64_t tag;
			bool shared;
			bool dirty;
			unsigned char data[CACHELINE_SZ];
			genattr_extension genattr;
		};

		IACECache(tlm_utils::simple_initiator_socket<cache_ace>& init_socket) :
			m_cacheline(new CacheLine[NUM_CACHELINES]),
			m_init_socket(init_socket),
			m_ongoing_gp(NULL),
			m_toggle(false)
		{}

		virtual ~IACECache()
		{
			delete[] m_cacheline;
		}

		virtual void handle_load(tlm::tlm_generic_payload& gp) = 0;
		virtual void handle_store(tlm::tlm_generic_payload& gp) = 0;

		bool handle_readonce(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			genattr_extension *genattr;

			gp.get_extension(genattr);
			if (!genattr) {
				return false;
			}

			if (in_cache(addr)) {
				CacheLine *l = get_line(addr);

				l->shared = true;
				genattr->set_shared(true);

				read_line(gp, 0);
				genattr->set_datatransfer();
			}

			return true;
		}

		bool handle_readshared(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			genattr_extension *genattr;

			gp.get_extension(genattr);
			if (!genattr) {
				return false;
			}

			if (in_cache(addr)) {
				CacheLine *l = get_line(addr);

				assert(l);

				if (l->shared == false) {
					genattr->set_was_unique();
				}

				l->shared = true;
				genattr->set_shared();

				if (l->dirty) {
					genattr->set_dirty();
					l->dirty = false;
				}

				read_line(gp, 0);
				genattr->set_datatransfer();
			}

			return true;
		}

		bool handle_readclean(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			genattr_extension *genattr;

			gp.get_extension(genattr);
			if (!genattr) {
				return false;
			}

			if (in_cache(addr)) {
				CacheLine *l = get_line(addr);

				assert(l);

				if (l->shared == false) {
					genattr->set_was_unique();
				}

				l->shared = true;
				genattr->set_shared();

				read_line(gp, 0);
				genattr->set_datatransfer();
			}

			return true;
		}

		bool handle_readnotshareddirty(tlm::tlm_generic_payload& gp)
		{
			// Handle as readclean
			return handle_readclean(gp);
		}

		bool handle_readunique(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			genattr_extension *genattr;

			gp.get_extension(genattr);
			if (!genattr) {
				return false;
			}

			if (in_cache(addr)) {
				CacheLine *l = get_line(addr);

				assert(l);

				if (l->shared == false) {
					genattr->set_was_unique();
				}

				if (l->dirty) {
					genattr->set_dirty();
				}

				genattr->set_shared(false);

				read_line(gp, 0);
				genattr->set_datatransfer();

				l->valid = false;
				m_monitor.reset(addr);
			}

			return true;
		}

		bool handle_cleaninvalid(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			genattr_extension *genattr;

			gp.get_extension(genattr);
			if (!genattr) {
				return false;
			}

			if (in_cache(addr)) {
				CacheLine *l = get_line(addr);

				assert(l);

				if (l->shared == false) {
					genattr->set_was_unique();
				}

				if (l->dirty) {
					read_line(gp, 0);
					genattr->set_datatransfer();
					genattr->set_dirty();
				}

				l->valid = false;
				m_monitor.reset(addr);

				genattr->set_shared(false);
			}

			return true;
		}

		bool handle_cleanshared(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			genattr_extension *genattr;

			gp.get_extension(genattr);
			if (!genattr) {
				return false;
			}

			if (in_cache(addr)) {
				CacheLine *l = get_line(addr);

				assert(l);

				if (l->shared == false) {
					genattr->set_was_unique();
				}

				if (l->dirty) {
					read_line(gp, 0);
					genattr->set_datatransfer();
					genattr->set_dirty();
					l->dirty = false;
				}

				l->shared = true;
				genattr->set_shared();
			}

			return true;
		}

		bool handle_makeinvalid(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			genattr_extension *genattr;

			gp.get_extension(genattr);
			if (!genattr) {
				return false;
			}

			if (in_cache(addr)) {
				CacheLine *l = get_line(addr);

				assert(l);

				l->valid = false;
				m_monitor.reset(addr);
			}

			return true;
		}

		bool write_ongoing()
		{
			return OngoingWriteBack() || OngoingWriteClean() || OngoingEvict();
		}

		bool OngoingWriteBack()
		{
			if (m_ongoing_gp && m_ongoing_gp->is_write()) {
				genattr_extension *genattr;

				m_ongoing_gp->get_extension(genattr);
				if (genattr && genattr->get_snoop() == AW::WriteBack) {
					return true;
				}
			}
			return false;
		}

		bool OngoingWriteClean()
		{
			if (m_ongoing_gp && m_ongoing_gp->is_write()) {
				genattr_extension *genattr;

				m_ongoing_gp->get_extension(genattr);
				if (genattr && genattr->get_snoop() == AW::WriteClean) {
					return true;
				}
			}
			return false;
		}

		bool OngoingEvict()
		{
			if (m_ongoing_gp && m_ongoing_gp->is_write()) {
				genattr_extension *genattr;

				m_ongoing_gp->get_extension(genattr);
				if (genattr && genattr->get_snoop() == AW::Evict) {
					return true;
				}
			}
			return false;
		}

		sc_event& WriteDoneEvent() { return m_write_done_event; }

		inline unsigned int get_line_offset(uint64_t addr)
		{
			return addr & (CACHELINE_SZ-1);
		}

		inline uint64_t align_address(uint64_t addr)
		{
			return addr & ~(CACHELINE_SZ-1);
		}

		inline uint64_t get_tag(uint64_t addr)
		{
			return align_address(addr);
		}

		inline uint64_t get_index(uint64_t tag)
		{
			return (tag % CACHE_SZ) / CACHELINE_SZ;
		}

		CacheLine *get_line(uint64_t addr)
		{
			uint64 tag = get_tag(addr);
			unsigned int index = get_index(tag);

			return &m_cacheline[index];
		}

		// Tag must have been checked before calling this function
		unsigned int read_line(tlm::tlm_generic_payload& gp, unsigned int pos)
		{
			unsigned char *data = gp.get_data_ptr() + pos;
			uint64_t addr = gp.get_address() + pos;
			unsigned int len = gp.get_data_length() - pos;
			unsigned int line_offset = get_line_offset(addr);
			unsigned int max_len = CACHELINE_SZ - line_offset;
			unsigned char *be = gp.get_byte_enable_ptr();
			unsigned int be_len = gp.get_byte_enable_length();
			CacheLine *l = get_line(addr);

			if (len > max_len) {
				len = max_len;
			}

			if (be_len) {
				int i;

				for (i = 0; i < len; i++, pos++) {
					bool do_access = be[pos % be_len] == TLM_BYTE_ENABLED;

					if (do_access) {
						data[i] = l->data[line_offset + i];
					}
				}
			} else {
				memcpy(data, &l->data[line_offset], len);
			}

			return len;
		}

		// Tag must have been checked before calling this function
		unsigned int write_line(tlm::tlm_generic_payload& gp,
					unsigned int pos, bool dirty = true)
		{
			unsigned char *data = gp.get_data_ptr() + pos;
			uint64_t addr = gp.get_address() + pos;
			unsigned int len = gp.get_data_length() - pos;
			unsigned int line_offset = get_line_offset(addr);
			unsigned int max_len = CACHELINE_SZ - line_offset;
			unsigned char *be = gp.get_byte_enable_ptr();
			unsigned int be_len = gp.get_byte_enable_length();
			CacheLine *l = get_line(addr);

			if (len > max_len) {
				len = max_len;
			}

			if (be_len) {
				int i;

				for (i = 0; i < len; i++, pos++) {
					bool do_access = be[pos % be_len] == TLM_BYTE_ENABLED;

					if (do_access) {
						l->data[line_offset + i] = data[i];
					}
				}
			} else {
				memcpy(&l->data[line_offset], data, len);
			}

			l->dirty = dirty;

			return len;
		}

		bool in_cache(uint64_t addr)
		{
			uint64 tag = get_tag(addr);
			CacheLine *l = get_line(addr);

			if (!l->valid) {
				return false;
			}

			return l->tag == tag;
		}

		bool do_tlm(tlm::tlm_generic_payload& gp_org,
				genattr_extension& attr,
				unsigned int pos = 0,
				unsigned int len = 0)
		{
			unsigned char *data = gp_org.get_data_ptr() + pos;
			uint64_t addr = gp_org.get_address() + pos;
			uint32_t be_len = gp_org.get_byte_enable_length();
			uint8_t be[be_len];
			genattr_extension *genattr;
			sc_time delay(SC_ZERO_TIME);
			tlm::tlm_generic_payload gp;

			if (len == 0) {
				len = gp_org.get_data_length();
			}

			genattr = new genattr_extension();
			genattr->copy_from(attr);

			gp.set_command(gp_org.get_command());

			gp.set_address(addr);

			gp.set_data_length(len);
			gp.set_data_ptr(data);

			if (be_len >= gp_org.get_data_length()) {
				gp.set_byte_enable_ptr(gp_org.get_byte_enable_ptr() + pos);
				gp.set_byte_enable_length(len);
			} else {
				unsigned int i;
				uint8_t *be_org = gp_org.get_byte_enable_ptr();

				for (i = 0; i < be_len; i++) {
					unsigned int be_idx = pos + i;

					be[i] = be_org[be_idx % be_len];
				}

				gp.set_byte_enable_ptr(be);
				gp.set_byte_enable_length(be_len);
			}

			gp.set_streaming_width(len);

			gp.set_dmi_allowed(false);
			gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

			gp.set_extension(genattr);

			m_ongoing_gp = &gp;

			m_init_socket->b_transport(gp, delay);

			m_ongoing_gp = NULL;

			if (gp.is_write()) {
				m_write_done_event.notify();
			}

			attr.copy_from(*genattr);

			return gp.get_response_status() == tlm::TLM_OK_RESPONSE;
		}

		bool do_tlm(tlm::tlm_command cmd, uint64_t addr, unsigned char *data,
				unsigned int len, genattr_extension& attr)
		{
			genattr_extension *genattr;
			sc_time delay(SC_ZERO_TIME);
			tlm::tlm_generic_payload gp;

			genattr = new genattr_extension();
			genattr->copy_from(attr);

			gp.set_command(cmd);

			gp.set_address(addr);

			gp.set_data_length(len);
			gp.set_data_ptr(data);

			gp.set_byte_enable_ptr(NULL);
			gp.set_byte_enable_length(0);

			gp.set_streaming_width(len);

			gp.set_dmi_allowed(false);
			gp.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

			gp.set_extension(genattr);

			m_ongoing_gp = &gp;

			m_init_socket->b_transport(gp, delay);

			m_ongoing_gp = NULL;

			if (gp.is_write()) {
				m_write_done_event.notify();
			}

			attr.copy_from(*genattr);

			return gp.get_response_status() == tlm::TLM_OK_RESPONSE;
		}

		//
		// Transactions
		//

		void writeback(CacheLine *l, tlm::tlm_generic_payload& gp)
		{
			genattr_extension genattr;
			genattr_extension *attr;

			assert(l && l->valid && l->dirty);

			// Leave as normal access (Sec 3.1.5 [1])
			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			gp.get_extension(attr);
			if (attr) {
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_qos(l->genattr.get_qos());
			genattr.set_secure(l->genattr.get_secure());
			genattr.set_region(l->genattr.get_region());

			genattr.set_domain(Domain::Inner);
			genattr.set_snoop(AW::WriteBack);

			if (do_tlm(tlm::TLM_WRITE_COMMAND,
					l->tag,
					l->data,
					CACHELINE_SZ,
					genattr)) {
				l->valid = false;
			}
		}

		void writeclean(CacheLine *l, tlm::tlm_generic_payload& gp)
		{
			genattr_extension genattr;
			genattr_extension *attr;

			assert(l && l->valid && l->dirty);

			// Leave as normal access (Sec 3.1.5 [1])
			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			gp.get_extension(attr);
			if (attr) {
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_qos(l->genattr.get_qos());
			genattr.set_secure(l->genattr.get_secure());
			genattr.set_region(l->genattr.get_region());

			genattr.set_domain(Domain::Inner);
			genattr.set_snoop(AW::WriteClean);

			if (do_tlm(tlm::TLM_WRITE_COMMAND,
					l->tag,
					l->data,
					CACHELINE_SZ,
					genattr)) {
				l->dirty = false;
			}
		}

		void write_line_unique(tlm::tlm_generic_payload& gp,
					unsigned int pos)
		{
			genattr_extension genattr;
			genattr_extension *attr;

			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			genattr.set_secure(false);

			gp.get_extension(attr);
			if (attr) {
				// Leave as normal access (Sec 3.1.5 [1])

				genattr.set_qos(attr->get_qos());
				genattr.set_secure(attr->get_secure());
				genattr.set_region(attr->get_region());
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_domain(Domain::Inner);
			genattr.set_snoop(AW::WriteLineUnique);

			if (do_tlm(gp, genattr, pos, CACHELINE_SZ)) {
				uint64_t addr = gp.get_address() + pos;

				//
				// Only update the line if in UC or SC
				// state [1] (C4.8.3 WriteLineUnique)
				//
				if (in_cache(addr)) {
					CacheLine *l = get_line(addr);

					this->write_line(gp, pos, false);

					l->shared = true;
				}
			}
		}

		void write_unique(tlm::tlm_generic_payload& gp,
					unsigned int pos, unsigned int len)
		{
			genattr_extension genattr;
			genattr_extension *attr;

			// Leave as normal access (Sec 3.1.5 [1])
			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			genattr.set_secure(false);

			gp.get_extension(attr);
			if (attr) {
				// Leave as normal access (Sec 3.1.5 [1])

				genattr.set_qos(attr->get_qos());
				genattr.set_secure(attr->get_secure());
				genattr.set_region(attr->get_region());
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_domain(Domain::Inner);
			genattr.set_snoop(AW::WriteUnique);

			if (do_tlm(gp, genattr, pos, len)) {
				uint64_t addr = gp.get_address();

				//
				// Only update lines that are in UC or SC
				// state [1] (C4.8.2 WriteUnique)
				//
				while (pos < len) {
					uint64_t addr = gp.get_address() + pos;
					unsigned int line_offset = get_line_offset(addr);
					unsigned int max_len = CACHELINE_SZ - line_offset;
					unsigned int n = len;

					if (n > max_len) {
						n = max_len;
					}

					if (in_cache(addr)) {
						CacheLine *l = get_line(addr);

						this->write_line(gp, pos, false);

						l->shared = true;
					}

					//
					// Move to next cacheline
					//
					pos+=n;
					addr+=n;
				}
			}
		}

		void write_no_snoop(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			genattr_extension genattr;
			genattr_extension *attr;

			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			genattr.set_secure(false);

			gp.get_extension(attr);
			if (attr) {
				genattr.set_exclusive(attr->get_exclusive());
				genattr.set_qos(attr->get_qos());
				genattr.set_secure(attr->get_secure());
				genattr.set_region(attr->get_region());
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_domain(Domain::NonSharable);
			genattr.set_snoop(AW::WriteNoSnoop);

			//
			// Non shareable are not cached
			//
			assert(in_cache(addr) == false);

			if (do_tlm(gp, genattr)) {
				gp.set_response_status(tlm::TLM_OK_RESPONSE);
			} else {
				gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
			}
		}

		void evict(CacheLine *l, tlm::tlm_generic_payload& gp)
		{
			genattr_extension genattr;
			genattr_extension *attr;

			assert(l && l->valid);

			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			gp.get_extension(attr);
			if (attr) {
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_qos(l->genattr.get_qos());
			genattr.set_secure(l->genattr.get_secure());
			genattr.set_region(l->genattr.get_region());

			genattr.set_domain(Domain::Inner);
			genattr.set_snoop(AW::Evict);

			if (do_tlm(tlm::TLM_WRITE_COMMAND,
					l->tag,
					l->data,
					CACHELINE_SZ,
					genattr)) {
				l->valid = false;
			}
		}

		void read_not_shared_dirty(tlm::tlm_generic_payload& gp,
						uint64_t addr)
		{
			uint64 tag = get_tag(addr);
			CacheLine *l = get_line(addr);
			unsigned int len = CACHELINE_SZ;
			genattr_extension genattr;
			genattr_extension *attr;

			if (l->valid) {
				if (l->dirty && get_toggle()) {
					writeclean(l, gp);
				}
				if (l->dirty) {
					writeback(l, gp);
				} else {
					evict(l, gp);
				}
			}

			genattr.set_secure(false);
			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			genattr.set_secure(false);

			gp.get_extension(attr);
			if (attr) {
				genattr.set_exclusive(attr->get_exclusive());
				genattr.set_qos(attr->get_qos());
				genattr.set_secure(attr->get_secure());
				genattr.set_region(attr->get_region());
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_domain(Domain::Inner);
			genattr.set_snoop(AR::ReadNotSharedDirty);

			if (do_tlm(tlm::TLM_READ_COMMAND,
					tag,
					l->data,
					len,
					genattr)) {
				l->valid = true;
				l->tag = tag;
				l->shared = genattr.get_shared();
				l->dirty = genattr.get_dirty();

				l->genattr.copy_from(genattr);
			}
		}

		void read_shared(tlm::tlm_generic_payload& gp,
					uint64_t addr)
		{
			uint64 tag = get_tag(addr);
			CacheLine *l = get_line(addr);
			unsigned int len = CACHELINE_SZ;
			genattr_extension genattr;
			genattr_extension *attr;

			if (l->valid) {
				if (l->dirty && get_toggle()) {
					writeclean(l, gp);
				}
				if (l->dirty) {
					writeback(l, gp);
				} else {
					evict(l, gp);
				}
			}

			genattr.set_secure(false);
			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			genattr.set_secure(false);

			gp.get_extension(attr);
			if (attr) {
				genattr.set_exclusive(attr->get_exclusive());
				genattr.set_qos(attr->get_qos());
				genattr.set_secure(attr->get_secure());
				genattr.set_region(attr->get_region());
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_domain(Domain::Inner);
			genattr.set_snoop(AR::ReadShared);

			if (do_tlm(tlm::TLM_READ_COMMAND,
					tag,
					l->data,
					len,
					genattr)) {
				l->valid = true;
				l->tag = tag;
				l->shared = genattr.get_shared();
				l->dirty = genattr.get_dirty();

				l->genattr.copy_from(genattr);
			}

			if (attr) {
				bool exokay = genattr.get_exclusive_handled();

				attr->set_exclusive_handled(exokay);
			}
		}

		void read_no_snoop(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			genattr_extension genattr;
			genattr_extension *attr;

			genattr.set_secure(false);
			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			gp.get_extension(attr);
			if (attr) {
				genattr.set_exclusive(attr->get_exclusive());
				genattr.set_qos(attr->get_qos());
				genattr.set_secure(attr->get_secure());
				genattr.set_region(attr->get_region());
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_domain(Domain::NonSharable);
			genattr.set_snoop(AR::ReadNoSnoop);

			//
			// Non shareable are not cached
			//
			assert(in_cache(addr) == false);

			if (do_tlm(gp, genattr)) {
				gp.set_response_status(tlm::TLM_OK_RESPONSE);
			} else {
				gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
			}
		}

		void read_unique(tlm::tlm_generic_payload& gp,
					uint64_t addr)
		{
			uint64_t tag = get_tag(addr);
			CacheLine *l = get_line(addr);
			unsigned int len = CACHELINE_SZ;
			genattr_extension genattr;
			genattr_extension *attr;

			if (l->valid) {
				if (l->dirty && get_toggle()) {
					writeclean(l, gp);
				}
				if (l->dirty) {
					writeback(l, gp);
				} else {
					evict(l, gp);
				}
			}

			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			genattr.set_secure(false);

			gp.get_extension(attr);
			if (attr) {
				// Leave as normal access (Sec 3.1.5 [1])

				genattr.set_qos(attr->get_qos());
				genattr.set_secure(attr->get_secure());
				genattr.set_region(attr->get_region());
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_domain(Domain::Inner);
			genattr.set_snoop(AR::ReadUnique);

			if (do_tlm(tlm::TLM_READ_COMMAND,
					tag,
					l->data,
					len,
					genattr)) {
				l->valid = true;
				l->tag = tag;
				l->shared = genattr.get_shared();
				l->dirty = genattr.get_dirty();

				l->genattr.copy_from(genattr);
			}
		}

		void make_unique(tlm::tlm_generic_payload& gp,
					uint64_t addr)
		{
			uint64_t tag = get_tag(addr);
			CacheLine *l = get_line(addr);
			unsigned int len = CACHELINE_SZ;
			genattr_extension genattr;
			genattr_extension *attr;

			if (l->valid) {
				if (l->dirty && get_toggle()) {
					writeclean(l, gp);
				}
				if (l->dirty) {
					writeback(l, gp);
				} else {
					evict(l, gp);
				}
			}

			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			genattr.set_secure(false);

			gp.get_extension(attr);
			if (attr) {
				// Leave as normal access (Sec 3.1.5 [1])

				genattr.set_qos(attr->get_qos());
				genattr.set_secure(attr->get_secure());
				genattr.set_region(attr->get_region());
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_domain(Domain::Inner);
			genattr.set_snoop(AR::MakeUnique);

			if (do_tlm(tlm::TLM_READ_COMMAND,
					tag,
					l->data,
					len,
					genattr)) {
				l->valid = true;
				l->tag = tag;

				l->shared = false;
				l->dirty = true;

				l->genattr.copy_from(genattr);
			}
		}

		void read_clean(tlm::tlm_generic_payload& gp,
				uint64_t addr)
		{
			uint64_t tag = get_tag(addr);
			CacheLine *l = get_line(addr);
			unsigned int len = CACHELINE_SZ;
			genattr_extension genattr;
			genattr_extension *attr;

			if (l->valid) {
				if (l->dirty && get_toggle()) {
					writeclean(l, gp);
				}
				if (l->dirty) {
					writeback(l, gp);
				} else {
					evict(l, gp);
				}
			}

			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			genattr.set_secure(false);

			gp.get_extension(attr);
			if (attr) {
				genattr.set_exclusive(attr->get_exclusive());
				genattr.set_qos(attr->get_qos());
				genattr.set_secure(attr->get_secure());
				genattr.set_region(attr->get_region());
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_domain(Domain::Inner);
			genattr.set_snoop(AR::ReadClean);

			if (do_tlm(tlm::TLM_READ_COMMAND,
					tag,
					l->data,
					len,
					genattr)) {
				l->valid = true;
				l->tag = tag;
				l->shared = genattr.get_shared();
				l->dirty = false;

				l->genattr.copy_from(genattr);
			}

			if (attr) {
				bool exokay = genattr.get_exclusive_handled();

				attr->set_exclusive_handled(exokay);
			}
		}

		void clean_unique(tlm::tlm_generic_payload& gp,
					uint64_t addr)
		{
			CacheLine *l = get_line(addr);
			unsigned int len = CACHELINE_SZ;
			genattr_extension genattr;
			genattr_extension *attr;

			genattr.set_bufferable(true);
			genattr.set_modifiable(true);
			genattr.set_read_allocate(true);
			genattr.set_write_allocate(true);

			genattr.set_secure(false);

			gp.get_extension(attr);
			if (attr) {
				genattr.set_exclusive(attr->get_exclusive());
				genattr.set_qos(attr->get_qos());
				genattr.set_secure(attr->get_secure());
				genattr.set_region(attr->get_region());
				genattr.set_transaction_id(attr->get_transaction_id());
			}

			genattr.set_domain(Domain::Inner);
			genattr.set_snoop(AR::CleanUnique);

			if (do_tlm(tlm::TLM_READ_COMMAND,
					l->tag,
					l->data,
					len,
					genattr)) {

				//
				// Overlapping CleanUnique, Sections C4.10.3
				// C4.6.1 [1]
				//
				if (l->valid) {
					l->shared = false;
				} else {
					//
					// snoop filter awarenes evict
					//
					l->valid = true;
					evict(l, gp);
				}
			}

			if (l->valid && attr) {
				bool exokay = genattr.get_exclusive_handled();

				attr->set_exclusive_handled(exokay);
			}
		}

		bool is_unique(uint64_t addr)
		{
			CacheLine *l = get_line(addr);

			return l->shared == false;
		}

		unsigned int to_write(tlm::tlm_generic_payload& gp,
					unsigned int pos)
		{
			uint64_t addr = gp.get_address() + pos;
			unsigned int len = gp.get_data_length() - pos;
			unsigned int line_offset = this->get_line_offset(addr);
			unsigned int max_len = CACHELINE_SZ - line_offset;

			if (len > max_len) {
				len = max_len;
			}

			return len;
		}

		bool is_exclusive(tlm::tlm_generic_payload& trans)
		{
			genattr_extension *genattr;

			trans.get_extension(genattr);
			if (genattr) {
				return genattr->get_exclusive();
			}
			return false;
		}

		bool has_exokay(tlm::tlm_generic_payload& trans)
		{
			genattr_extension *genattr;

			trans.get_extension(genattr);
			if (genattr) {
				return genattr->get_exclusive_handled();
			}
			return false;
		}

		bool clear_exokay(tlm::tlm_generic_payload& trans)
		{
			genattr_extension *genattr;

			trans.get_extension(genattr);
			if (genattr) {
				genattr->set_exclusive_handled(false);
			}
			return false;
		}

		void set_exokay(tlm::tlm_generic_payload& trans)
		{
			genattr_extension *genattr;

			trans.get_extension(genattr);
			if (genattr) {
				genattr->set_exclusive_handled(true);
			}
		}

		bool get_toggle()
		{
			m_toggle = (m_toggle) ? false : true;

			return m_toggle;
		}

		//
		// Master exclusive monitor Section C9.2 [1]
		//
		class MasterExclusiveMonitor
		{
		public:
			void set(uint64_t addr)
			{
				if (!in_list(addr)) {
					m_addr.push_back(addr);
				}
			}

			void reset(uint64_t addr)
			{
				if (in_list(addr)) {
					m_addr.remove(addr);
				}
			}

			bool is_set(uint64_t addr)
			{
				return in_list(addr);
			}

			void writeclean_dirty(uint64_t addr,
					tlm::tlm_generic_payload& gp)
			{
				CacheLine l = get_line(addr);

				if (l->dirty) {
					this->writeclean(l, gp);
				}
			}

		private:

			bool in_list(uint64_t addr)
			{
				typename std::list<uint64_t>::iterator it;

				for (it = m_addr.begin();
					it != m_addr.end(); it++) {

					if ((*it) == addr) {
						return true;
					}
				}
				return false;
			}

			std::list<uint64_t> m_addr;
		};

	protected:
		CacheLine *m_cacheline;
		tlm_utils::simple_initiator_socket<cache_ace>& m_init_socket;
		tlm::tlm_generic_payload *m_ongoing_gp;
		sc_event m_write_done_event;
		bool m_toggle;
		MasterExclusiveMonitor m_monitor;
	};

	class ACECacheWriteBack : public IACECache
	{
	public:
		ACECacheWriteBack(tlm_utils::simple_initiator_socket<cache_ace>& init_socket) :
			IACECache(init_socket)
		{}

		void handle_load(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			unsigned int len = gp.get_data_length();
			unsigned int pos = 0;
			bool exclusive = this->is_exclusive(gp);
			bool exclusive_failed = false;

			while (pos < len) {
				if (this->in_cache(addr)) {
					unsigned int n = this->read_line(gp, pos);
					pos+=n;
					addr+=n;
				} else {
					bool do_read_shared = 
						exclusive || this->get_toggle();

					//
					// toggle between read_shared /
					// read_not_shared_dirty if not
					// exclusive
					//
					if (do_read_shared) {
						this->read_shared(gp, addr);
					} else {
						this->read_not_shared_dirty(gp, addr);
					}

					if (exclusive) {
						if (!this->has_exokay(gp)) {
							exclusive_failed = true;
						}
						this->m_monitor.set(addr);
						this->clear_exokay(gp);
					}
				}
			}

			if (exclusive && !exclusive_failed) {
				this->set_exokay(gp);
			}

			gp.set_response_status(tlm::TLM_OK_RESPONSE);
		}

		void handle_store(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			unsigned int len = gp.get_data_length();
			unsigned int pos = 0;
			bool exclusive = this->is_exclusive(gp);

			while (pos < len) {
				if (exclusive &&
					!this->m_monitor.is_set(addr)) {
					break;
				}

				if (this->in_cache(addr)){
					if (this->is_unique(addr)) {
						unsigned int n = this->write_line(gp, pos);

						if (exclusive) {
							//
							// Exclusive sequence
							// for the line done
							//
							this->m_monitor.reset(addr);
						}

						pos+=n;
						addr+=n;
					} else {
						//
						// Overlapping ReadUnique /
						// CleanInvalid / MakeInvalid
						// on the snoop channel forces
						// a recheck of in_cache [1]
						//
						this->clean_unique(gp, addr);

						if (exclusive) {
							// exclusive failed
							if (!this->has_exokay(gp)) {
								this->m_monitor.reset(addr);
								break;
							}
							this->clear_exokay(gp);
						}
					}
				} else {
					unsigned int n;

					n = this->to_write(gp, pos);

					if (n == CACHELINE_SZ) {
						this->make_unique(gp, addr);
					} else {
						this->read_unique(gp, addr);
					}
				}
			}

			if (exclusive && pos == len) {
				this->set_exokay(gp);
			}

			gp.set_response_status(tlm::TLM_OK_RESPONSE);
		}
	};

	class ACECacheWriteThrough : public IACECache
	{
	public:
		ACECacheWriteThrough(tlm_utils::simple_initiator_socket<cache_ace>& init_socket) :
			IACECache(init_socket)
		{}

		void handle_load(tlm::tlm_generic_payload& gp)
		{
			uint64_t addr = gp.get_address();
			unsigned int len = gp.get_data_length();
			unsigned int pos = 0;

			while (pos < len) {
				if (this->in_cache(addr)) {
					unsigned int n = this->read_line(gp, pos);
					pos+=n;
					addr+=n;
				} else {
					this->read_clean(gp, addr);
				}
			}

			// Exclusive not supported
			this->clear_exokay(gp);

			gp.set_response_status(tlm::TLM_OK_RESPONSE);
		}

		void handle_store(tlm::tlm_generic_payload& gp)
		{
			unsigned int len = gp.get_data_length();
			unsigned int pos = 0;

			while (pos < len) {
				unsigned int n = this->to_write(gp, pos);
				bool do_write_line_unique =  false;

				//
				// Do WriteLineUnique if it is a cacheline
				// sized transaction with no sparse wstrb
				//
				if (n == CACHELINE_SZ) {
					do_write_line_unique = no_sparse_wstrb(gp, pos);
				}

				if (do_write_line_unique) {
					this->write_line_unique(gp, pos);
				} else {
					this->write_unique(gp, pos, n);
				}

				pos+=n;
			}

			// Exclusive not supported
			this->clear_exokay(gp);

			gp.set_response_status(tlm::TLM_OK_RESPONSE);
		}

	private:
		bool no_sparse_wstrb(tlm::tlm_generic_payload& gp, unsigned int pos)
		{
			uint8_t *be = gp.get_byte_enable_ptr();
			uint32_t be_len = gp.get_byte_enable_length();

			if (be_len) {
				int end_pos = pos + CACHELINE_SZ;

				for (pos; pos < end_pos; pos++) {
					bool do_access = be[pos % be_len] == TLM_BYTE_ENABLED;

					if (!do_access) {
						return false;
					}
				}
			}

			return true;
		}
	};

	bool is_barrier(tlm::tlm_generic_payload& gp)
	{
		genattr_extension *genattr;

		gp.get_extension(genattr);
		if (!genattr) {
			return false;
		}

		return genattr->get_barrier();
	}

	bool is_dvm(tlm::tlm_generic_payload& gp)
	{
		if (gp.is_read()) {
			genattr_extension *genattr;

			gp.get_extension(genattr);
			if (genattr) {
				uint8_t domain = genattr->get_domain();

				switch(genattr->get_snoop()) {
				case AR::DVMComplete:
				case AR::DVMMessage:
					if (domain == Domain::Inner ||
						domain == Domain::Outer) {
						return true;
					}
					// Fallthrough else
				default:
					// Return false
					break;
				}
			}
		}
		return false;
	}

	bool in_non_shareable_region(tlm::tlm_generic_payload& gp)
	{
		uint64_t addr = gp.get_address();
		uint64_t m_nonshareable_end = m_nonshareable_start + m_nonshareable_len;

		return addr >= m_nonshareable_start &&
			addr < m_nonshareable_end;
	}

	virtual void b_transport(tlm::tlm_generic_payload& trans,
				sc_time& delay)
	{

		if (is_barrier(trans) || is_dvm(trans)) {
			init_socket->b_transport(trans, delay);
		} else if (in_non_shareable_region(trans)) {
			if (trans.is_write()) {
				m_cache->write_no_snoop(trans);
			} else if (trans.is_read()){
				m_cache->read_no_snoop(trans);
			}
		} else {
			if (trans.is_write()) {
				m_cache->handle_store(trans);
			} else if (trans.is_read()){
				m_cache->handle_load(trans);
			}
		}
	}

	virtual void b_transport_snoop(tlm::tlm_generic_payload& gp,
				sc_time& delay)
	{
		genattr_extension *genattr;

		//
		// Check if a WriteBack or WriteClean is in progress (to any
		// address), if so wait for the transaction to end before
		// answering the snoop (Section C5.2.5 [1]).
		//
		// The master is also permitted to wait for Evict writes
		// (Section C5.2.5 [1]), which is used above aswell so lets do
		// that.
		//
		while (m_cache->write_ongoing()) {
			wait(m_cache->WriteDoneEvent());
		}

		// Default response
		gp.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);

		gp.get_extension(genattr);

		if (genattr) {
			bool res = false;

			switch (genattr->get_snoop()) {
			case AC::ReadOnce:
				res = m_cache->handle_readonce(gp);
				break;
			case AC::ReadShared:
				res = m_cache->handle_readshared(gp);
				break;
			case AC::ReadClean:
				res = m_cache->handle_readclean(gp);
				break;
			case AC::ReadNotSharedDirty:
				res = m_cache->handle_readnotshareddirty(gp);
				break;
			case AC::ReadUnique:
				res = m_cache->handle_readunique(gp);
				break;
			case AC::CleanInvalid:
				res = m_cache->handle_cleaninvalid(gp);
				break;
			case AC::CleanShared:
				res = m_cache->handle_cleanshared(gp);
				break;
			case AC::MakeInvalid:
				res = m_cache->handle_makeinvalid(gp);
				break;
			case AC::DVMMessage:
				//
				// Launch a DVM complete reply to DVM Sync
				// commands
				//
				if (IsDVMSyncCmd(gp.get_address())) {
					sc_spawn(sc_bind(
						&cache_ace::reply_dvm_complete,
						this));
				}
				res = true;
				break;
			case AC::DVMComplete:
				res = true;
				break;
			default:
				break;
			}

			if (res) {
				gp.set_response_status(tlm::TLM_OK_RESPONSE);
			}
		}
	}

	void init_dvm_complete_gp()
	{
		//
		// Setup according to C12.6 [1]
		//
		m_dvm_complete_gp.set_command(tlm::TLM_READ_COMMAND);
		m_dvm_complete_gp.set_address(0);

		// Bridge corrects this to be according to C12.6 [1]
		m_dvm_complete_gp.set_data_length(1);
		m_dvm_complete_gp.set_data_ptr(&m_dummy_data);

		m_dvm_complete_gp.set_byte_enable_ptr(NULL);
		m_dvm_complete_gp.set_byte_enable_length(0);

		m_dvm_complete_gp.set_streaming_width(1);

		m_dvm_complete_gp.set_dmi_allowed(false);

		m_genattr->set_secure(false);
		m_genattr->set_modifiable(true);
		m_genattr->set_barrier(true);
		m_genattr->set_domain(Domain::Inner);
		m_genattr->set_snoop(AR::DVMComplete);

		//
		// m_dvm_complete_gp will delete genattr
		//
		m_dvm_complete_gp.set_extension(m_genattr);
	}

	void reply_dvm_complete()
	{
		sc_time delay(SC_ZERO_TIME);

		m_dvm_complete_gp.set_response_status(
					tlm::TLM_INCOMPLETE_RESPONSE);

		init_socket->b_transport(m_dvm_complete_gp, delay);
	}

	enum {
		DVMCmdShift = 12,
		DVMCmdMask = 0x7,

		DVMCmdSync = 0x4,
	};

	bool IsDVMSyncCmd(uint64_t addr)
	{
		uint64_t cmd = (addr >> DVMCmdShift) & DVMCmdMask;

		return cmd == DVMCmdSync;
	}

	uint64_t m_nonshareable_start;
	unsigned int m_nonshareable_len;

	tlm::tlm_generic_payload m_dvm_complete_gp;
	genattr_extension *m_genattr;
	unsigned char m_dummy_data;

	IACECache *m_cache;
};
#endif /* TLM_CACHE_ACE_H__ */
