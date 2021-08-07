/******************************************************************************
 *                                                                            *
 * Copyright 2019 Jan Henrik Weinstock                                        *
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

#ifndef VCML_PROTOCOLS_TLM_SBI_H
#define VCML_PROTOCOLS_TLM_SBI_H

#include "vcml/common/types.h"
#include "vcml/common/report.h"
#include "vcml/common/systemc.h"

namespace vcml {

    struct tlm_sbi {
        union {
            struct {
                bool is_debug:1;
                bool is_nodmi:1;
                bool is_sync:1;
                bool is_insn:1;
                bool is_excl:1;
                bool is_lock:1;

                int cpuid:20;
                int level:20;
            };

            u64 code;
        };

        tlm_sbi() = default;
        tlm_sbi(const tlm_sbi&) = default;
        tlm_sbi(tlm_sbi&&) = default;

        tlm_sbi(bool debug, bool nodmi, bool sync, bool insn, bool excl,
                 bool lock, int cpu = 0, int lvl = 0):
             is_debug(debug), is_nodmi(nodmi), is_sync(sync), is_insn(insn),
             is_excl(excl), is_lock(lock), cpuid(cpu), level(lvl) {
        }

        tlm_sbi& operator  = (const tlm_sbi& other);
        tlm_sbi& operator &= (const tlm_sbi& other);
        tlm_sbi& operator |= (const tlm_sbi& other);

        tlm_sbi operator & (const tlm_sbi& other) const;
        tlm_sbi operator | (const tlm_sbi& other) const;

        bool operator == (const tlm_sbi& other) const;
        bool operator != (const tlm_sbi& other) const;
    };

    static_assert(sizeof(tlm_sbi) == sizeof(u64), "sideband too large");

    inline tlm_sbi& tlm_sbi::operator  = (const tlm_sbi& other) {
        code = other.code;
        return *this;
    }

    inline tlm_sbi& tlm_sbi::operator &= (const tlm_sbi& other) {
        code &= other.code;
        return *this;
    }

    inline tlm_sbi& tlm_sbi::operator |= (const tlm_sbi& other) {
        code |= other.code;
        return *this;
    }

    inline tlm_sbi tlm_sbi::operator & (const tlm_sbi& other) const {
        tlm_sbi result(*this);
        return result &= other;
    }

    inline tlm_sbi tlm_sbi::operator | (const tlm_sbi& other) const {
        tlm_sbi result(*this);
        return result |= other;
    }

    inline bool tlm_sbi::operator == (const tlm_sbi& other) const {
        return code == other.code;
    }

    inline bool tlm_sbi::operator != (const tlm_sbi& other) const {
        return code != other.code;
    }

    const tlm_sbi SBI_NONE  = { false, false, false, false, false, false };
    const tlm_sbi SBI_DEBUG = { true,  false, false, false, false, false };
    const tlm_sbi SBI_NODMI = { false, true,  false, false, false, false };
    const tlm_sbi SBI_SYNC  = { false, false, true,  false, false, false };
    const tlm_sbi SBI_INSN  = { false, false, false, true,  false, false };
    const tlm_sbi SBI_EXCL  = { false, false, false, false, true,  false };
    const tlm_sbi SBI_LOCK  = { false, false, false, false, false, true  };

    inline tlm_sbi SBI_CPUID(int cpu) {
        return tlm_sbi(false, false, false, false, false, false, cpu, 0);
    }

    inline tlm_sbi SBI_LEVEL(int lvl) {
        return tlm_sbi(false, false, false, false, false, false, 0, lvl);
    }

    class sbiext: public tlm_extension<sbiext>,
                  public tlm_sbi {
    public:
        sbiext() = default;

        virtual tlm_extension_base* clone() const override;
        virtual void copy_from(const tlm_extension_base& ext) override;
    };

    inline bool tx_has_sbi(const tlm_generic_payload& tx) {
        return tx.get_extension<sbiext>() != nullptr;
    }

    inline const tlm_sbi& tx_get_sbi(const tlm_generic_payload& tx) {
        return tx_has_sbi(tx) ? *tx.get_extension<sbiext>() : SBI_NONE;
    }

    inline bool tx_is_debug(const tlm_generic_payload& tx) {
        return tx_get_sbi(tx).is_debug;
    }

    inline bool tx_is_nodmi(const tlm_generic_payload& tx) {
        return tx_get_sbi(tx).is_nodmi;
    }

    inline bool tx_is_sync(const tlm_generic_payload& tx) {
        return tx_get_sbi(tx).is_sync;
    }

    inline bool tx_is_insn(const tlm_generic_payload& tx) {
        return tx_get_sbi(tx).is_insn;
    }

    inline bool tx_is_excl(const tlm_generic_payload& tx) {
        return tx_get_sbi(tx).is_excl;
    }

    inline bool tx_is_lock(const tlm_generic_payload& tx) {
        return tx_get_sbi(tx).is_lock;
    }

    inline int tx_cpuid(const tlm_generic_payload& tx) {
        return tx_get_sbi(tx).cpuid;
    }

    inline int tx_level(const tlm_generic_payload& tx) {
        return tx_get_sbi(tx).level;
    }

    void tx_set_sbi(tlm_generic_payload& tx, const tlm_sbi& info);
    void tx_set_cpuid(tlm_generic_payload& tx, int id);
    void tx_set_level(tlm_generic_payload& tx, int lvl);

}

#endif
