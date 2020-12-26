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

#ifndef VCML_REGISTER_H
#define VCML_REGISTER_H

#include "vcml/common/types.h"
#include "vcml/common/bitops.h"
#include "vcml/common/report.h"
#include "vcml/common/systemc.h"

#include "vcml/logging/logger.h"
#include "vcml/backends/backend.h"
#include "vcml/properties/property.h"

#include "vcml/range.h"
#include "vcml/sbi.h"
#include "vcml/dmi_cache.h"
#include "vcml/component.h"
#include "vcml/register.h"

namespace vcml {

    class peripheral;

    class reg_base: public sc_object
    {
    private:
        range       m_range;
        vcml_access m_access;
        bool        m_rsync;
        bool        m_wsync;
        peripheral* m_host;

    public:
        u64 get_address() const { return m_range.start; }
        u64 get_size() const { return m_range.length(); }
        const range& get_range() const { return m_range; }

        vcml_access get_access() const { return m_access; }
        void set_access(vcml_access a) { m_access = a; }

        bool is_read_only() const;
        bool is_write_only() const;

        bool is_readable() const { return is_read_allowed(m_access); }
        bool is_writeable() const { return is_write_allowed(m_access); }

        void allow_read_only()  { m_access = VCML_ACCESS_READ; }
        void allow_write_only() { m_access = VCML_ACCESS_WRITE; }
        void allow_read_write() { m_access = VCML_ACCESS_READ_WRITE; }

        void sync_on_read(bool sync = true) { m_rsync = sync; }
        void sync_on_write(bool sync = true) { m_wsync = sync; }

        void sync_always() { m_rsync = m_wsync = true; }
        void sync_never() { m_rsync = m_wsync = false; }

        peripheral* get_host() { return m_host; }

        reg_base(const char* nm, u64 addr, u64 size, peripheral* h = nullptr);
        virtual ~reg_base();

        VCML_KIND(reg_base);

        virtual void reset() = 0;

        unsigned int receive(tlm_generic_payload& tx, const sideband& info);

        virtual void do_read(const range& addr, void* ptr) = 0;
        virtual void do_write(const range& addr, const void* ptr) = 0;
    };

    inline bool reg_base::is_read_only() const {
        return m_access == VCML_ACCESS_READ;
    }

    inline bool reg_base::is_write_only() const {
        return m_access == VCML_ACCESS_WRITE;
    }

    template <class HOST, typename DATA, const unsigned int N = 1>
    class reg: public reg_base,
               public property<DATA, N>
    {
    private:
        HOST* m_host;
        bool m_banked;
        DATA m_init[N];
        std::map<int, DATA*> m_banks;

        void init_bank(int bank);

    public:
        typedef DATA (HOST::*readfunc)  (void);
        typedef DATA (HOST::*writefunc) (DATA);

        readfunc read;
        writefunc write;

        typedef DATA (HOST::*tagged_readfunc)  (unsigned int);
        typedef DATA (HOST::*tagged_writefunc) (DATA, unsigned int);

        unsigned int tag;

        tagged_readfunc tagged_read;
        tagged_writefunc tagged_write;

        bool is_banked() const { return m_banked; }
        void set_banked(bool set = true) { m_banked = set; }

        const DATA& bank(int bank) const;
        DATA& bank(int bank);

        const DATA& bank(int bank, unsigned int idx) const;
        DATA& bank(int bank, unsigned int idx);

        const DATA& current_bank(unsigned int idx = 0) const;
        DATA& current_bank(unsigned int idx = 0);

        const char* name() const { return sc_core::sc_object::name(); }

        reg(const char* nm, u64 addr, DATA init = DATA(),
            HOST* host = nullptr);
        virtual ~reg();
        reg() = delete;

        VCML_KIND(reg);

        virtual void reset() override;

        virtual void do_read(const range& addr, void* ptr) override;
        virtual void do_write(const range& addr, const void* ptr) override;

        operator DATA() const;

        const DATA& operator [] (unsigned int idx) const;
        DATA& operator [] (unsigned int idx);

        DATA operator ++ (int);
        DATA operator -- (int);

        reg<HOST, DATA, N>& operator ++ ();
        reg<HOST, DATA, N>& operator -- ();

        template <typename T> reg<HOST, DATA, N>& operator =  (T value);
        template <typename T> reg<HOST, DATA, N>& operator |= (T value);
        template <typename T> reg<HOST, DATA, N>& operator &= (T value);
        template <typename T> reg<HOST, DATA, N>& operator ^= (T value);
        template <typename T> reg<HOST, DATA, N>& operator += (T value);
        template <typename T> reg<HOST, DATA, N>& operator -= (T value);
        template <typename T> reg<HOST, DATA, N>& operator *= (T value);
        template <typename T> reg<HOST, DATA, N>& operator /= (T value);

        template <typename T> bool operator == (T other) const;
        template <typename T> bool operator != (T other) const;
        template <typename T> bool operator <= (T other) const;
        template <typename T> bool operator >= (T other) const;
        template <typename T> bool operator  < (T other) const;
        template <typename T> bool operator  > (T other) const;

        template <typename F> DATA get_bitfield(F field);
        template <typename F, typename T> void set_bitfield(F field, T val);
    };

    template <class HOST, typename DATA, const unsigned int N>
    void reg<HOST, DATA, N>::init_bank(int bank) {
        VCML_ERROR_ON(!m_banked, "cannot create banks in register %s", name());

        if (bank == 0 || stl_contains(m_banks, bank))
            return;

        m_banks[bank] = new DATA[N];
        for (unsigned int i = 0; i < N; i++)
            m_banks[bank][i] = m_init[i];
    }

    template <class HOST, typename DATA, const unsigned int N>
    const DATA& reg<HOST, DATA, N>::bank(int bk) const {
        return bank(bk, 0);
    }

    template <class HOST, typename DATA, const unsigned int N>
    DATA& reg<HOST, DATA, N>::bank(int bk) {
        return bank(bk, 0);
    }

    template <class HOST, typename DATA, const unsigned int N>
    const DATA& reg<HOST, DATA, N>::bank(int bk,  unsigned int idx) const {
        VCML_ERROR_ON(idx >= N, "index %d out of bounds", idx);
        if (bk == 0 || !m_banked)
            return property<DATA, N>::get(idx);
        if (!stl_contains(m_banks, bk))
            return property<DATA, N>::get_default();
        return m_banks.at(bk)[idx];
    }

    template <class HOST, typename DATA, const unsigned int N>
    DATA& reg<HOST, DATA, N>::bank(int bk, unsigned int idx) {
        VCML_ERROR_ON(idx >= N, "index %d out of bounds", idx);
        if (bk == 0 || !m_banked)
            return property<DATA, N>::get(idx);
        if (!stl_contains(m_banks, bk))
            init_bank(bk);
        return m_banks[bk][idx];
    }

    template <class HOST, typename DATA, const unsigned int N>
    const DATA& reg<HOST, DATA, N>::current_bank(unsigned int idx) const {
        return bank(m_host->current_cpu(), idx);
    }

    template <class HOST, typename DATA, const unsigned int N>
    DATA& reg<HOST, DATA, N>::current_bank(unsigned int idx) {
        return bank(m_host->current_cpu(), idx);
    }

    template <class HOST, typename DATA, const unsigned int N>
    reg<HOST, DATA, N>::reg(const char* n, u64 addr, DATA def, HOST* h):
        reg_base(n, addr, N * sizeof(DATA), h),
        property<DATA, N>(n, def, h),
        m_host(h),
        m_banked(false),
        m_init(),
        m_banks(),
        read(nullptr),
        write(nullptr),
        tag(0),
        tagged_read(nullptr),
        tagged_write(nullptr) {
        for (unsigned int i = 0; i < N; i++)
            m_init[i] = property<DATA, N>::get(i);

        if (m_host == nullptr)
            m_host = dynamic_cast<HOST*>(get_host());
        VCML_ERROR_ON(!m_host, "invalid host specified for register %s", n);
    }

    template <class HOST, typename DATA, const unsigned int N>
    reg<HOST, DATA, N>::~reg() {
        for (auto bank : m_banks)
            delete [] bank.second;
    }

    template <class HOST, typename DATA, const unsigned int N>
    void reg<HOST, DATA, N>::reset() {
        for (unsigned int i = 0; i < N; i++)
            property<DATA, N>::set(m_init[i], i);

        for (auto bank : m_banks) {
            for (unsigned int i = 0; i < N; i++)
                m_banks[bank.first][i] = m_init[i];
        }
    }

    template <class HOST, typename DATA, const unsigned int N>
    void reg<HOST, DATA, N>::do_read(const range& txaddr,  void* ptr) {
        range addr(txaddr);
        unsigned char* dest = (unsigned char*)ptr;

        while (addr.start <= addr.end) {
            u64 idx  = (addr.start - get_address()) / sizeof(DATA);
            u64 off  = (addr.start - get_address()) % sizeof(DATA);
            u64 size = min(addr.length(), (u64)sizeof(DATA));

            DATA val = current_bank(idx);

            if (tagged_read != nullptr)
                val = (m_host->*tagged_read)(N > 1 ? idx : tag);
            else if (read != nullptr)
                val = (m_host->*read)();

            current_bank(idx) = val;

            unsigned char* ptr = (unsigned char*)&val + off;
            memcpy(dest, ptr, size);

            addr.start += size;
            dest += size;
        }
    }

    template <class HOST, typename DATA, const unsigned int N>
    void reg<HOST, DATA, N>::do_write(const range& txaddr, const void* data) {
        range addr(txaddr);
        const unsigned char* src = (const unsigned char*)data;

        while (addr.start <= addr.end) {
            u64 idx  = (addr.start - get_address()) / sizeof(DATA);
            u64 off  = (addr.start - get_address()) % sizeof(DATA);
            u64 size = min(addr.length(), (u64)sizeof(DATA));

            DATA val = current_bank(idx);

            unsigned char* ptr = (unsigned char*)&val + off;
            memcpy(ptr, src, size);

            if (tagged_write != nullptr)
                val = (m_host->*tagged_write)(val, N > 1 ? idx : tag);
            else if (write != nullptr)
                val = (m_host->*write)(val);

            current_bank(idx) = val;

            addr.start += size;
            src += size;
        }
    }

    template <class HOST, typename DATA, const unsigned int N>
    reg<HOST, DATA, N>::operator DATA() const {
        return current_bank();
    }

    template <class HOST, typename DATA, const unsigned int N>
    const DATA& reg<HOST, DATA, N>::operator [] (unsigned int idx) const {
        return current_bank(idx);
    }

    template <class HOST, typename DATA, const unsigned int N>
    DATA& reg<HOST, DATA, N>::operator [] (unsigned int idx) {
        return current_bank(idx);
    }

    template <class HOST, typename DATA, const unsigned int N>
    DATA reg<HOST, DATA, N>::operator ++ (int unused) {
        (void) unused;
        DATA result = current_bank();

        for (unsigned int i = 0; i < N; i++)
            ++current_bank(i);

        return result;
    }

    template <class HOST, typename DATA, const unsigned int N>
    DATA reg<HOST, DATA, N>::operator -- (int unused) {
        (void) unused;
        DATA result = current_bank();

        for (unsigned int i = 0; i < N; i++)
            --current_bank(i);

        return result;
    }

    template <class HOST, typename DATA, const unsigned int N>
    reg<HOST, DATA, N>& reg<HOST, DATA, N>::operator ++ () {
        for (unsigned int i = 0; i < N; i++)
            current_bank(i)++;
        return *this;
    }

    template <class HOST, typename DATA, const unsigned int N>
    reg<HOST, DATA, N>& reg<HOST, DATA, N>::operator -- () {
        for (unsigned int i = 0; i < N; i++)
            current_bank(i)--;
        return *this;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    reg<HOST, DATA, N>& reg<HOST, DATA, N>::operator = (T value) {
        for (unsigned int i = 0; i < N; i++)
            current_bank(i) = value;
        return *this;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    reg<HOST, DATA, N>& reg<HOST, DATA, N>::operator |= (T value) {
        for (unsigned int i = 0; i < N; i++)
            current_bank(i) |= value;
        return *this;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    reg<HOST, DATA, N>& reg<HOST, DATA, N>::operator &= (T value) {
        for (unsigned int i = 0; i < N; i++)
            current_bank(i) &= value;
        return *this;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    reg<HOST, DATA, N>& reg<HOST, DATA, N>::operator ^= (T value) {
        for (unsigned int i = 0; i < N; i++)
            current_bank(i) ^= value;
        return *this;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    reg<HOST, DATA, N>& reg<HOST, DATA, N>::operator += (T value) {
        for (unsigned int i = 0; i < N; i++)
            current_bank(i) += value;
        return *this;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    reg<HOST, DATA, N>& reg<HOST, DATA, N>::operator -= (T value) {
        for (unsigned int i = 0; i < N; i++)
            current_bank(i) -= value;
        return *this;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    reg<HOST, DATA, N>& reg<HOST, DATA, N>::operator *= (T value) {
        for (unsigned int i = 0; i < N; i++)
            current_bank(i) *= value;
        return *this;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    reg<HOST, DATA, N>& reg<HOST, DATA, N>::operator /= (T value) {
        for (unsigned int i = 0; i < N; i++)
            current_bank(i) /= value;
        return *this;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    inline bool reg<HOST, DATA, N>::operator == (T other) const {
        for (unsigned int i = 0; i < N; i++)
            if (current_bank(i) != other)
                return false;
        return true;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    inline bool reg<HOST, DATA, N>::operator < (T other) const {
        for (unsigned int i = 0; i < N; i++)
            if (current_bank(i) >= other)
                return false;
        return true;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    inline bool reg<HOST, DATA, N>::operator > (T other) const {
        for (unsigned int i = 0; i < N; i++)
            if (current_bank(i) <= other)
                return false;
        return true;
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    inline bool reg<HOST, DATA, N>::operator != (T other) const {
        return !operator == (other);
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    inline bool reg<HOST, DATA, N>::operator <= (T other) const {
        return !operator > (other);
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename T>
    inline bool reg<HOST, DATA, N>::operator >= (T other) const {
        return !operator < (other);
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename F>
    inline DATA reg<HOST, DATA, N>::get_bitfield(F field) {
        return get_bitfield(field, current_bank());
    }

    template <class HOST, typename DATA, const unsigned int N>
    template <typename F, typename T>
    inline void reg<HOST, DATA, N>::set_bitfield(F field, T val) {
        for (unsigned int i = 0; i < N; i++)
            set_bitfield(field, current_bank(i));
    }
}

#define VCML_LOG_REG_BIT_CHANGE(bit, reg, val) do {               \
    if ((reg & bit) != (val & bit))                               \
        log_debug(#bit " bit %s", val & bit ? "set" : "cleared"); \
} while (0)

#endif
