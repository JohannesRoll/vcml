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

#ifndef VCML_LOG_TERM_H
#define VCML_LOG_TERM_H

#include "vcml/common/types.h"
#include "vcml/logging/logger.h"

namespace vcml {

    class log_term: public logger
    {
    private:
        bool     m_use_colors;
        ostream& m_os;

    public:
        inline bool using_colors() const;
        inline void set_colors(bool set = true);

        log_term(bool use_cerr = true);
        virtual ~log_term();

        virtual void write_log(const logmsg& msg) override;

        static const char* colors[NUM_LOG_LEVELS];
        static const char* reset;
    };

    inline bool log_term::using_colors() const {
        return m_use_colors;
    }

    inline void log_term::set_colors(bool set) {
        m_use_colors = set;
    }

}

#endif
