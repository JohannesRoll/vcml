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

#include <sys/mman.h>

#include "vcml/models/generic/memory.h"

namespace vcml { namespace generic {

    struct image_info {
        string file;
        u64 offset;
    };

    static vector<image_info> images_from_string(string s) {
        vector<image_info> images;
        s.erase(std::remove_if(s.begin(), s.end(), [] (unsigned char c) {
            return isspace(c);
        }), s.end());

        vector<string> token = split(s, ';');
        for (string cur : token) {
            if (cur.empty())
                continue;

            vector<string> vec = split(cur, '@');
            if (vec.empty())
                continue;

            string file = vec[0];
            u64 off = vec.size() > 1 ? strtoull(vec[1].c_str(), NULL, 0) : 0;
            images.push_back({file, off});
        }

        return images;
    }

    bool memory::cmd_load(const vector<string>& args, ostream& os) {
        string binary = args[0];
        u64 offset = 0ull;

        if (args.size() > 1)
            offset = strtoull(args[1].c_str(), NULL, 0);

        load(binary, offset);
        return true;
    }

    bool memory::cmd_show(const vector<string>& args, ostream& os) {
        u64 start = strtoull(args[0].c_str(), NULL, 0);
        u64 end = strtoull(args[1].c_str(), NULL, 0);

        if ((end <= start) || (end >= size))
            return false;

        #define HEX(x, w) std::setfill('0') << std::setw(w) << \
                          std::hex << x << std::dec
        os << "showing range 0x" << HEX(start, 8) << " .. 0x" << HEX(end, 8);

        u64 addr = start & ~0xf;
        while (addr < end) {
            if ((addr % 16) == 0)
                os << "\n" << HEX(addr, 8) << ":";
            if ((addr % 4) == 0)
                os << " ";
            if (addr >= start)
                os << HEX((unsigned int)m_memory[addr], 2) << " ";
            else
                os << "   ";
            addr++;
        }

        #undef HEX
        return true;
    }

    memory::memory(const sc_module_name& nm, u64 sz, bool read_only,
                   unsigned int alignment, unsigned int rl, unsigned int wl):
        peripheral(nm, host_endian(), rl, wl),
        m_base(nullptr),
        m_memory(nullptr),
        size("size", sz),
        align("align", alignment),
        readonly("readonly", read_only),
        images("images", ""),
        poison("poison", 0x00),
        IN("IN") {
        VCML_ERROR_ON(size == 0u, "memory size cannot be 0");
        VCML_ERROR_ON(align >= 64u, "requested alignment too big");

        const int perms = PROT_READ | PROT_WRITE;
        const int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;

        std::uintptr_t extra = (1ull << align) - 1;
        m_base = mmap(0, size + extra, perms, flags, -1, 0);
        VCML_ERROR_ON(m_base == MAP_FAILED, "mmap failed: %s", strerror(errno));
        m_memory = (u8*)(((std::uintptr_t)m_base + extra) & ~extra);

        map_dmi(m_memory, 0, size - 1, readonly ? VCML_ACCESS_READ
                                                : VCML_ACCESS_READ_WRITE);

        register_command("load", 1, this, &memory::cmd_load,
            "Load <binary> [off] to load the contents of file <binary> to "
            "relative offset [off] in memory (off is zero if unspecified).");
        register_command("show", 2, this, &memory::cmd_show,
            "Show memory contents between addresses [start] and [end]. "
            "Usage: show [start] [end]");
    }

    memory::~memory() {
        if (m_base)
            munmap(m_base, size);
    }

    void memory::reset() {
        if (poison > 0)
            memset(m_memory, poison, size);

        vector<image_info> imagevec = images_from_string(images);
        for (auto ii : imagevec) {
            log_debug("loading '%s' to 0x%08lx", ii.file.c_str(), ii.offset);
            load(ii.file, ii.offset);
        }
    }

    void memory::load(const string& binary, u64 offset) {
        ifstream file(binary.c_str(), std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            log_warn("cannot open file '%s'", binary.c_str());
            return;
        }

        if (offset >= size) {
            log_warn("offset %lu exceeds memsize %lu", offset, size.get());
            return;
        }

        u64 nbytes = file.tellg();
        if (nbytes > size - offset) {
            nbytes = size - offset;
            log_warn("image file '%s' to big, truncating after %lu bytes",
                     binary.c_str(), nbytes);
        }

        file.seekg(0, std::ios::beg);
        file.read((char*)(m_memory + offset), nbytes);
    }

    tlm_response_status memory::read(const range& addr, void* data,
                                     const sideband& info) {
        if (addr.end >= size)
            return TLM_ADDRESS_ERROR_RESPONSE;
        memcpy(data, m_memory + addr.start, addr.length());
        return TLM_OK_RESPONSE;
    }

    tlm_response_status memory::write(const range& addr, const void* data,
                                      const sideband& info) {
        if (addr.end >= size)
            return TLM_ADDRESS_ERROR_RESPONSE;
        if (readonly && !info.is_debug)
            return TLM_COMMAND_ERROR_RESPONSE;
        memcpy(m_memory + addr.start, data, addr.length());
        return TLM_OK_RESPONSE;
    }

}}
