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

#include "vcml/models/generic/spibus.h"

namespace vcml { namespace generic {

    spibus::spibus(const sc_module_name& nm):
        component(nm),
        spi_host(),
        SPI_IN("SPI_IN"),
        SPI_OUT("SPI_OUT"),
        CS("CS") {
    }

    spibus::~spibus() {
        // nothing to do
    }

    void spibus::reset() {
        component::reset();
    }

    bool spibus::is_valid(unsigned int port) const {
        if (!CS.exists(port) || !SPI_OUT.exists(port))
            return false;
        if (!stl_contains(m_csmode, port))
            return false;
        return true;
    }

    bool spibus::is_active(unsigned int port) const {
        if (!is_valid(port))
            return false;
        return CS[port].read() == m_csmode.at(port);
    }

    bool spibus::is_active_high(unsigned int port) const {
        if (!is_valid(port))
            return false;
        return m_csmode.at(port);
    }

    bool spibus::is_active_low(unsigned int port) const {
        if (!is_valid(port))
            return false;
        return !m_csmode.at(port);
    }

    void spibus::spi_transport(const spi_target_socket&, spi_payload& spi) {
        for (auto port : CS) {
            if (is_active(port.first))
                SPI_OUT[port.first].transport(spi);
        }
    }

    unsigned int spibus::next_free() const {
        unsigned int idx = 0;
        while (SPI_OUT.exists(idx) || CS.exists(idx))
            VCML_ERROR_ON(++idx == 0, "no free ports");
        return idx;
    }

    void spibus::bind(spi_initiator_socket& initiator) {
        SPI_IN.bind(initiator);
    }

    unsigned int spibus::bind(spi_target_socket& target, sc_signal<bool>& cs,
                              bool cs_active_high) {
        unsigned int port = next_free();
        SPI_OUT[port].bind(target);
        CS[port].bind(cs);
        m_csmode[port] = cs_active_high;
        return port;
    }

}}
