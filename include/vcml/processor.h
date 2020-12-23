/******************************************************************************
 *                                                                            *
 * Copyright 2018 Jan Henrik Weinstock                                        *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License");            *
 * you may not use this file except in compliance with the License.           *
 * You may obtain a copy of the License at                                    *
 *                                                                            *
 *     http://www.apache.org/licenses/LICENSE-2.0                             *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 *                                                                            *
 ******************************************************************************/

#ifndef VCML_PROCESSOR_H
#define VCML_PROCESSOR_H

#include "vcml/common/types.h"
#include "vcml/common/report.h"
#include "vcml/common/bitops.h"

#include "vcml/logging/logger.h"
#include "vcml/backends/backend.h"
#include "vcml/properties/property.h"

#include "vcml/debugging/gdbstub.h"
#include "vcml/debugging/gdbserver.h"

#include "vcml/elf.h"
#include "vcml/range.h"
#include "vcml/ports.h"
#include "vcml/component.h"
#include "vcml/master_socket.h"

namespace vcml {

    struct irq_stats {
        unsigned int irq;
        unsigned int irq_count;
        bool         irq_status;
        sc_time      irq_last;
        sc_time      irq_uptime;
        sc_time      irq_longest;
    };

    struct cpureg {
        int          regno;
        int          gdbno;
        const char*  name;
        unsigned int size;
        int          perms;
    };

    class processor: public component,
                     protected debugging::gdbstub {
    private:
        double m_run_time;
        u64    m_cycle_count;
        elf*   m_symbols;

        debugging::gdbserver* m_gdb;

        std::map<unsigned int, irq_stats> m_irq_stats;

        struct cpureg_info: public cpureg {
            property_base* prop;

            void create(u64 defval);
            u64  get()const ;
            void set(u64 val);

            bool read_allowed()  const { return is_read_allowed(perms); }
            bool write_allowed() const { return is_write_allowed(perms); }
        };

        vcml_endian m_endian;
        std::map<int, cpureg_info> m_cpuregs;
        u64 m_num_gdbregs;

        bool cmd_dump(const vector<string>& args, ostream& os);
        bool cmd_read(const vector<string>& args, ostream& os);
        bool cmd_symbols(const vector<string>& args, ostream& os);
        bool cmd_lsym(const vector<string>& args, ostream& os);
        bool cmd_disas(const vector<string>& args, ostream& os);
        bool cmd_v2p(const vector<string>& args, ostream& os);

        bool has_cpureg(int regno) const;
        bool lookup_cpureg(int regno, cpureg_info& reg);
        bool lookup_gdbreg(int regno, cpureg_info& reg);

        u64  get_cpureg_internal(const cpureg_info& reg);
        void set_cpureg_internal(const cpureg_info& reg, u64 val);

        void processor_thread();

        void irq_handler(unsigned int irq);

    public:
        property<string> symbols;

        property<u16>  gdb_port;
        property<bool> gdb_wait;
        property<bool> gdb_sync;
        property<bool> gdb_echo;

        in_port_list<bool> IRQ;

        master_socket INSN;
        master_socket DATA;

        processor(const sc_module_name& name);
        virtual ~processor();
        VCML_KIND(processor);

        processor() = delete;
        processor(const processor&) = delete;

        virtual void session_suspend() override;
        virtual void session_resume() override;

        virtual string disassemble(u64& addr, unsigned char* insn);

        virtual u64 get_program_counter() { return 0; }
        virtual u64 get_stack_pointer()   { return 0; }
        virtual u64 get_core_id()         { return 0; }

        virtual void set_program_counter(u64 val) {}
        virtual void set_stack_pointer(u64 val)   {}
        virtual void set_core_id(u64 val)         {}

        virtual u64 cycle_count() const = 0;

        double get_run_time() const { return m_run_time; }
        double get_cps()      const { return cycle_count() / m_run_time; }

        virtual void reset() override;

        bool get_irq_stats(unsigned int irq, irq_stats& stats) const;

        template <typename T>
        inline tlm_response_status fetch (u64 addr, T& data);

        template <typename T>
        inline tlm_response_status read  (u64 addr, T& data);

        template <typename T>
        inline tlm_response_status write (u64 addr, const T& data);

        vcml_endian get_endian() const { return m_endian; }
        void set_endian(vcml_endian e) { m_endian = e; }

        void set_little_endian() { m_endian = VCML_ENDIAN_LITTLE; }
        void set_big_endian()    { m_endian = VCML_ENDIAN_BIG; }

        bool is_little_endian() const;
        bool is_big_endian() const;
        bool is_host_endian() const;

        virtual u64  get_cpureg(int regno);
        virtual void set_cpureg(int regno, u64 val);

        void fetch_cpuregs();
        void flush_cpuregs();

        void define_cpuregs(const vector<cpureg>& regs);

    protected:
        void log_bus_error(const master_socket& socket, vcml_access accss,
                           tlm_response_status rs, u64 addr, u64 size);

        virtual void interrupt(unsigned int irq, bool set);
        virtual void simulate(unsigned int cycles) = 0;
        virtual void update_local_time(sc_time& local_time) override;
        virtual void end_of_elaboration() override;

        virtual u64  gdb_num_registers() override;
        virtual u64  gdb_register_width(u64 idx) override;
        virtual bool gdb_read_reg(u64 idx, void* p, u64 size) override;
        virtual bool gdb_write_reg(u64 idx, const void* p, u64 sz) override;
        virtual bool gdb_page_size(u64& size) override;
        virtual bool gdb_virt_to_phys(u64 vaddr, u64& paddr) override;
        virtual bool gdb_read_mem(u64 addr, void* buf, u64 sz) override;
        virtual bool gdb_write_mem(u64 addr, const void* buf, u64 sz) override;
        virtual bool gdb_insert_breakpoint(u64 addr) override;
        virtual bool gdb_remove_breakpoint(u64 addr) override;
        virtual bool gdb_insert_watchpoint(const range& mem,
                                           vcml_access acs) override;
        virtual bool gdb_remove_watchpoint(const range& mem,
                                           vcml_access a) override;
        virtual string gdb_handle_rcmd(const string& command) override;

        virtual void gdb_simulate(unsigned int cycles) override;
        virtual void gdb_notify(int signal) override;
    };

    inline string processor::disassemble(u64& addr, unsigned char* insn) {
        addr += 4;
        return "n/a";
    }

    template <typename T>
    inline tlm_response_status processor::fetch(u64 addr, T& data) {
        tlm_response_status rs = INSN.readw(addr, data);
        if (failed(rs))
            log_bus_error(INSN, VCML_ACCESS_READ, rs, addr, sizeof(T));
        return rs;
    }

    template <typename T>
    inline tlm_response_status processor::read(u64 addr, T& data) {
        tlm_response_status rs = DATA.readw(addr, data);
        if (failed(rs))
            log_bus_error(DATA, VCML_ACCESS_READ, rs, addr, sizeof(T));
        return rs;
    }

    template <typename T>
    inline tlm_response_status processor::write(u64 addr, const T& data) {
        tlm_response_status rs = DATA.writew(addr, data);
        if (failed(rs))
            log_bus_error(DATA, VCML_ACCESS_WRITE, rs, addr, sizeof(T));
        return rs;
    }

    inline bool processor::is_little_endian() const {
        return m_endian == VCML_ENDIAN_LITTLE;
    }

    inline bool processor::is_big_endian() const {
        return m_endian == VCML_ENDIAN_BIG;
    }

    inline bool processor::is_host_endian() const {
        return m_endian == host_endian();
    }

}

#endif
