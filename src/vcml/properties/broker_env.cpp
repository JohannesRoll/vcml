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

#include "vcml/properties/broker_env.h"

namespace vcml {

broker_env::broker_env(): broker("environment") {
    // nothing to do
}

broker_env::~broker_env() {
    // nothing to do
}

bool broker_env::defines(const string& name) const {
    string nm = name;
    std::replace(nm.begin(), nm.end(), SC_HIERARCHY_CHAR, '_');
    return std::getenv(nm.c_str()) != nullptr;
}

bool broker_env::lookup(const string& name, string& val) {
    string nm = name;
    std::replace(nm.begin(), nm.end(), SC_HIERARCHY_CHAR, '_');

    const char* env = std::getenv(nm.c_str());
    if (env == nullptr)
        return false;

    const size_t max_size = string().max_size() - 1;
    if (strlen(env) > max_size)
        return false;

    val = string(env);
    return true;
}

} // namespace vcml
