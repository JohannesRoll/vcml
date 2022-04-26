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

#ifndef VCML_THCTL_H
#define VCML_THCTL_H

#include "vcml/common/types.h"
#include "vcml/common/systemc.h"

namespace vcml {

bool thctl_is_sysc_thread();
bool thctl_is_in_critical();

void thctl_enter_critical();
void thctl_exit_critical();
void thctl_suspend();

void thctl_yield();
void thctl_notify();
void thctl_block();

class thctl_guard
{
private:
    bool m_locking;

public:
    thctl_guard();
    ~thctl_guard();
};

inline thctl_guard::thctl_guard():
    m_locking(!thctl_is_sysc_thread() && !thctl_is_in_critical()) {
    if (m_locking)
        thctl_enter_critical();
}

inline thctl_guard::~thctl_guard() {
    if (m_locking)
        thctl_exit_critical();
}

} // namespace vcml

#endif
