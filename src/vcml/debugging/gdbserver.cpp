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

#include <signal.h> // for SIGTRAP
#include "vcml/debugging/gdbserver.h"

namespace vcml { namespace debugging {

    static inline int char2int(char c) {
        return ((c >= 'a') && (c <= 'f')) ? c - 'a' + 10 :
               ((c >= 'A') && (c <= 'F')) ? c - 'A' + 10 :
               ((c >= '0') && (c <= '9')) ? c - '0' :
               (c == '\0') ? 0 : -1;
    }

    static inline u64 str2int(const char* s, int n) {
        u64 val = 0;
        for (const char* c = s + n - 1; c >= s; c--) {
            val <<= 4;
            val |= char2int(*c);
        }
        return val;
    }

    static inline u8 char_unescape(const char*& s) {
        u8 result = *s++;
        if (result == '}')
            result = *s++ ^ 0x20;
        return result;
    }

    void gdbserver::update_status(gdb_status status) {
        if (m_status == status)
            return;

        m_status = status;
        resume();
    }

    bool gdbserver::is_suspend_requested() const {
        if (!m_sync)
            return false;
        return m_status == GDB_STOPPED;
    }

    bool gdbserver::access_pmem(bool iswr, u64 addr, u8 buffer[], u64 size) {
        try {
            return iswr ? m_stub->async_write_mem(addr, buffer, size)
                        : m_stub->async_read_mem(addr, buffer, size);
        } catch (report& r) {
            log_warn("gdb cannot access %lu bytes at address %lx: %s",
                     size, addr, r.message());
            return false;
        }
    }

    bool gdbserver::access_vmem(bool iswr, u64 addr, u8 buffer[], u64 size) {
        u64 page_size = 0;
        if (!m_stub->async_page_size(page_size))
            return access_pmem(iswr, addr, buffer, size);

        u64 end = addr + size;
        while (addr < end) {
            u64 pa = 0;
            u64 todo = min(end - addr, page_size - (addr % page_size));
            if (m_stub->async_virt_to_phys(addr, pa)) {
                access_pmem(iswr, pa, buffer, todo);
            } else {
                memset(buffer, 0xee, todo);
            }

            addr += todo;
            buffer += todo;
        }

        return true;
    }

    string gdbserver::handle_unknown(const char* command) {
        return "";
    }

    string gdbserver::handle_step(const char* command) {
        int signal = 0;
        update_status(GDB_STEPPING);
        while (m_status == GDB_STEPPING) {
            if ((signal = recv_signal(100))) {
                log_debug("received signal 0x%x", signal);
                m_status = GDB_STOPPED;
                m_signal = GDBSIG_TRAP;
                wait_for_suspend();
            }
        }

        return mkstr("S%02x", m_signal);
    }

    string gdbserver::handle_continue(const char* command) {
        int signal = 0;
        update_status(GDB_RUNNING);
        while (m_status == GDB_RUNNING) {
            if ((signal = recv_signal(100))) {
                log_debug("received signal 0x%x", signal);
                m_status = GDB_STOPPED;
                m_signal = GDBSIG_TRAP;
                wait_for_suspend();
            }
        }

        return mkstr("S%02x", m_signal);
    }

    string gdbserver::handle_detach(const char* command) {
        disconnect();
        return "";
    }

    string gdbserver::handle_kill(const char* command) {
        disconnect();
        update_status(GDB_KILLED);
        sc_stop();
        return "";
    }

    string gdbserver::handle_query(const char* command) {
        if (strncmp(command, "qSupported", strlen("qSupported")) == 0)
            return mkstr("PacketSize=%zx", PACKET_SIZE);
        else if (strncmp(command, "qAttached", strlen("qAttached")) == 0)
            return "1";
        else if (strncmp(command, "qOffsets", strlen("qOffsets")) == 0)
            return "Text=0;Data=0;Bss=0";
        else if (strncmp(command, "qRcmd", strlen("qRcmd")) == 0)
            return handle_rcmd(command);
        else
            return handle_unknown(command);
    }

    string gdbserver::handle_rcmd(const char* command) {
        return m_stub->async_handle_rcmd(command);
    }

    string gdbserver::handle_reg_read(const char* command) {
        unsigned int reg;
        if (sscanf(command, "p%x", &reg) != 1) {
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        u64 regsz = m_stub->async_register_width(reg);
        if (regsz == 0)
            return "xxxxxxxx"; // respond with "contents unknown"

        u8* buffer = new u8[regsz];
        bool ok = m_stub->async_read_reg(reg, buffer, regsz);

        stringstream ss;
        ss << std::hex << std::setfill('0');
        for (unsigned int byte = 0; byte < regsz; byte++) {
            if (ok) ss << std::setw(2) << (int)buffer[byte];
            else    ss << "xx";
        }

        delete [] buffer;
        return ss.str();
    }

    string gdbserver::handle_reg_write(const char* command) {
        unsigned int reg;
        if (sscanf(command, "P%x=", &reg) != 1) {
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        u64 regsz = m_stub->async_register_width(reg);
        if (regsz == 0)
            return "OK";

        const char* str = strchr(command, '=');
        if (str == NULL) {
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        str++; // step beyond '='
        if (strlen(str) != regsz * 2) { // need two hex chars per byte
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        u8* buffer = new u8[regsz];
        for (unsigned int byte = 0; byte < regsz; byte++, str += 2)
            buffer[byte] = char2int(str[0]) << 4 | char2int(str[1]);

        bool ok = m_stub->async_write_reg(reg, buffer, regsz);
        delete [] buffer;
        if (!ok) {
            log_warn("gdb cannot write register %u", reg);
            return ERR_INTERNAL;
        }

        return "OK";
    }

    string gdbserver::handle_reg_read_all(const char* command) {
        u64 nregs = m_stub->async_num_registers();
        stringstream ss;
        ss << std::hex << std::setfill('0');

        for (u64 reg = 0; reg < nregs; reg++) {
            u64 regsz = m_stub->async_register_width(reg);
            if (regsz == 0)
                continue;

            u8* buffer = new u8[regsz];
            bool ok = m_stub->async_read_reg(reg, buffer, regsz);
            for (u64 byte = 0; byte < regsz; byte++) {
                if (ok) ss << std::setw(2) << (int)buffer[byte];
                else    ss << "xx";
            }

            delete [] buffer;
        }

        return ss.str();
    }

    string gdbserver::handle_reg_write_all(const char* command) {
        u64 nregs = m_stub->async_num_registers();
        u64 bufsz = 0;
        for (u64 reg = 0; reg < nregs; reg++)
            bufsz += m_stub->async_register_width(reg) * 2;

        const char* str = command + 1;
        if (strlen(str) != bufsz) {
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        for (u64 reg = 0; reg < nregs; reg++) {
            u64 regsz = m_stub->async_register_width(reg);
            if (regsz == 0)
                continue;

            u8* buffer = new u8[regsz];
            for (u64 byte = 0; byte < regsz; byte++, str += 2)
                buffer[byte] = char2int(str[0]) << 4 | char2int(str[1]);
            if (!m_stub->async_write_reg(reg, buffer, regsz))
                log_warn("gdb cannot write register %lu", reg);
            delete [] buffer;
        }

        return "OK";
    }

    string gdbserver::handle_mem_read(const char* command) {
        unsigned long long addr, size;
        if (sscanf(command, "m%llx,%llx", &addr, &size) != 2) {
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        if (size > BUFFER_SIZE) {
            log_warn("too much data requested: %llu bytes", size);
            return ERR_PARAM;
        }

        stringstream ss;
        ss << std::hex << std::setfill('0');

        u8 buffer[BUFFER_SIZE];
        if (!access_vmem(false, addr, buffer, size))
            return ERR_UNKNOWN;

        for (unsigned int i = 0; i < size; i++)
            ss << std::setw(2) << (int)buffer[i];

        return ss.str();
    }

    string gdbserver::handle_mem_write(const char* command) {
        unsigned long long addr, size;
        if (sscanf(command, "M%llx,%llx", &addr, &size) != 2) {
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        if (size > BUFFER_SIZE) {
            log_warn("too much data requested: %llu bytes", size);
            return ERR_PARAM;
        }

        const char* data = strchr(command, ':');
        if (data == NULL) {
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        data++;

        u8 buffer[BUFFER_SIZE];
        for (unsigned int i = 0; i < size; i++)
            buffer[i] = str2int(data++, 2);

        if (!access_vmem(true, addr, buffer, size))
            return ERR_UNKNOWN;

        return "OK";
    }

    string gdbserver::handle_mem_write_bin(const char* command) {
        unsigned long long addr, size;
        if (sscanf(command, "X%llx,%llx:", &addr, &size) != 2) {
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        if (size > BUFFER_SIZE) {
            log_warn("too much data requested: %llu bytes", size);
            return ERR_PARAM;
        }

        if (size == 0)
            return "OK"; // empty load to test if binary write is supported

        const char* data = strchr(command, ':');
        if (data == NULL) {
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        data++;

        u8 buffer[BUFFER_SIZE];
        for (unsigned int i = 0; i < size; i++)
            buffer[i] = char_unescape(data);

        if (!access_vmem(true, addr, buffer, size))
            return ERR_UNKNOWN;

        return "OK";
    }

    string gdbserver::handle_breakpoint_set(const char* command) {
        unsigned long long type, addr, length;
        if (sscanf(command, "Z%llx,%llx,%llx", &type, &addr, &length) != 3) {
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        const range mem(addr, addr + length - 1);
        switch (type) {
        case GDB_BREAKPOINT_SW:
        case GDB_BREAKPOINT_HW:
            if (!m_stub->async_insert_breakpoint(addr))
                return ERR_INTERNAL;
            break;

        case GDB_WATCHPOINT_WRITE:
            if (!m_stub->async_insert_watchpoint(mem, VCML_ACCESS_WRITE))
                return ERR_INTERNAL;
            break;

        case GDB_WATCHPOINT_READ:
            if (!m_stub->async_insert_watchpoint(mem, VCML_ACCESS_READ))
                return ERR_INTERNAL;
            break;


        case GDB_WATCHPOINT_ACCESS:
            if (!m_stub->async_insert_watchpoint(mem, VCML_ACCESS_READ_WRITE))
                return ERR_INTERNAL;
            break;

        default:
            log_warn("unknown breakpoint type %llu", type);
            return ERR_COMMAND;
        }

        return "OK";
    }

    string gdbserver::handle_breakpoint_delete(const char* command) {
        unsigned long long type, addr, length;
        if (sscanf(command, "z%llx,%llx,%llx", &type, &addr, &length) != 3) {
            log_warn("malformed command '%s'", command);
            return ERR_COMMAND;
        }

        const range mem(addr, addr + length - 1);
        switch (type) {
        case GDB_BREAKPOINT_SW:
        case GDB_BREAKPOINT_HW:
            if (!m_stub->async_remove_breakpoint(addr))
                return ERR_INTERNAL;
            break;

        case GDB_WATCHPOINT_WRITE:
            if (!m_stub->async_remove_watchpoint(mem, VCML_ACCESS_WRITE))
                return ERR_INTERNAL;
            break;

        case GDB_WATCHPOINT_READ:
            if (!m_stub->async_remove_watchpoint(mem, VCML_ACCESS_READ))
                return ERR_INTERNAL;
            break;

        case GDB_WATCHPOINT_ACCESS:
            if (!m_stub->async_remove_watchpoint(mem, VCML_ACCESS_READ_WRITE))
                return ERR_INTERNAL;
            break;

        default:
            log_warn("unknown breakpoint type %llu", type);
            return ERR_COMMAND;
        }

        return "OK";
    }

    string gdbserver::handle_exception(const char* command) {
        return mkstr("S%02u", SIGTRAP);
    }

    string gdbserver::handle_thread(const char* command) {
        return "OK";
    }

    string gdbserver::handle_vcont(const char* command) {
        return "";
    }

    gdbserver::gdbserver(u16 port, gdbstub* stub, gdb_status status):
        rspserver(port),
        suspender("gdbserver"),
        m_stub(stub),
        m_status(status),
        m_default(status),
        m_sync(true),
        m_signal(-1),
        m_handler() {
        VCML_ERROR_ON(!stub, "no debug stub given");

        m_handler['q'] = &gdbserver::handle_query;

        m_handler['s'] = &gdbserver::handle_step;
        m_handler['c'] = &gdbserver::handle_continue;
        m_handler['D'] = &gdbserver::handle_detach;
        m_handler['k'] = &gdbserver::handle_kill;

        m_handler['p'] = &gdbserver::handle_reg_read;
        m_handler['P'] = &gdbserver::handle_reg_write;
        m_handler['g'] = &gdbserver::handle_reg_read_all;
        m_handler['G'] = &gdbserver::handle_reg_write_all;

        m_handler['m'] = &gdbserver::handle_mem_read;
        m_handler['M'] = &gdbserver::handle_mem_write;
        m_handler['X'] = &gdbserver::handle_mem_write_bin;

        m_handler['Z'] = &gdbserver::handle_breakpoint_set;
        m_handler['z'] = &gdbserver::handle_breakpoint_delete;

        m_handler['H'] = &gdbserver::handle_thread;
        m_handler['v'] = &gdbserver::handle_vcont;
        m_handler['?'] = &gdbserver::handle_exception;

        run_async();
    }

    gdbserver::~gdbserver() {
        /* nothing to do */
    }

    void gdbserver::simulate(unsigned int cycles) {
        while (cycles > 0) {
            suspender::handle_requests();

            switch (m_status) {
            case GDB_KILLED:
                return;

            case GDB_STOPPED:
                return;

            case GDB_STEPPING:
                m_stub->gdb_simulate(1);
                notify(GDBSIG_TRAP);
                cycles--;
                break;

            case GDB_RUNNING:
            default:
                m_stub->gdb_simulate(cycles);
                cycles = 0;
                break;
            }
        }
    }

    string gdbserver::handle_command(const string& command) {
        try {
            handler func = find_handler(command.c_str());
            return (this->*func)(command.c_str());
        } catch (report& rep) {
            vcml::logger::log(rep);
            return ERR_INTERNAL;
        } catch (std::exception& ex) {
            log_warn("%s", ex.what());
            return ERR_INTERNAL;
        }
    }

    void gdbserver::handle_connect(const char* peer) {
        log_debug("gdb connected to %s", peer);
        update_status(GDB_STOPPED);
    }

    void gdbserver::handle_disconnect() {
        log_debug("gdb disconnected");
        update_status(m_default);
    }

}}
