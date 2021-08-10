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

#ifndef VCML_BROKER_H
#define VCML_BROKER_H

#include "vcml/common/types.h"
#include "vcml/common/strings.h"

namespace vcml {

    class broker
    {
    private:
        struct value {
            string value;
            int uses;
        };

        std::map<string, struct value> m_values;

        virtual bool lookup(const string& name, string& value);

    public:
        broker();
        virtual ~broker();

        void add(const string& name, const string& value);

    private:
        static list<broker*> brokers;

        static void register_provider(broker* provider);
        static void unregister_provider(broker* provider);

    public:
        static bool init(const string& name, string& value);
    };

}

#endif
