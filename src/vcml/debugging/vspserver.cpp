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

#include "vcml/common/aio.h"
#include "vcml/common/thctl.h"
#include "vcml/common/utils.h"
#include "vcml/common/systemc.h"
#include "vcml/common/version.h"
#include "vcml/component.h"

#include "vcml/debugging/vspserver.h"
#include "vcml/debugging/target.h"
#include "vcml/serial/port.h"
#include "vcml/net/adapter.h"
#include "vcml/ui/input.h"

namespace vcml { namespace debugging {

    static vspserver* session = nullptr;

    static void cleanup_session() {
        if (session != nullptr)
            session->cleanup();
    }

    string vspserver::handle_none(const char* command) {
        return "";
    }

    string vspserver::handle_step(const char* command) {
        vector<string> args = split(command, ',');
        sc_time duration, next;
        if (args.size() > 1) { // step duration passed as argument
            stringstream ss(args[1]);
            double t; ss >> t;
            duration = sc_time(t, SC_SEC);
        } else if (sc_get_curr_simcontext()->next_time(next)) { // next event
            duration = next - sc_time_stamp();
        } else { // step entire quantum
            duration = tlm::tlm_global_quantum::instance().get();
        }

        resume_simulation(duration);
        return is_connected() ? "OK" : "";
    }

    string vspserver::handle_cont(const char* command) {
        resume_simulation(SC_MAX_TIME);
        return is_connected() ? "OK" : "";
    }

    static string xml_escape(const string& s) {
        stringstream ss;
        for (char c : s) {
            switch (c) {
            case '<': ss << "&lt;"; break;
            case '>': ss << "&gt;"; break;
            case '&': ss << "&amp;"; break;
            case '\'': ss << "&apos;"; break;
            case '\"': ss << "&quot;"; break;
            default:
                ss << c;
            }
        }

        return escape(ss.str(), ",");
    }

    static string attr_type(const sc_attr_base* attr) {
        const property_base* prop = dynamic_cast<const property_base*>(attr);
        return prop != nullptr ? prop->type() : "unknown";
    }

    static string attr_name(const sc_attr_base* attr) {
        string name = attr->name();
        size_t pos = name.find_last_of(SC_HIERARCHY_CHAR);
        return pos == string::npos ? name : name.substr(pos + 1);
    }

    static size_t attr_count(const sc_attr_base* attr) {
        const property_base* prop = dynamic_cast<const property_base*>(attr);
        return prop != nullptr ? prop->count() : 0;
    }

    static void list_object(ostream& os, sc_object* obj) {
        os << "<object"
           << " name=\"" << xml_escape(obj->basename()) << "\""
           << " kind=\"" << xml_escape(obj->kind()) << "\">";

        // list object attributes
        for (const sc_attr_base* attr : obj->attr_cltn()) {
            os << "<attribute"
               << " name=\"" << xml_escape(attr_name(attr)) << "\""
               << " type=\"" << xml_escape(attr_type(attr)) << "\""
               << " count=\"" << attr_count(attr) << "\""
               << " />";
        }


        // list object commands
        module* mod = dynamic_cast<module*>(obj);
        if (mod != nullptr) {
            for (const command_base* cmd : mod->get_commands()) {
                os << "<command"
                   << " name=\"" << xml_escape(cmd->name()) << "\""
                   << " argc=\"" << cmd->argc() << "\""
                   << " desc=\"" << xml_escape(cmd->desc()) << "\""
                   << " />";
            }
        }

        // list object children
        for (sc_object* child : obj->get_child_objects())
            list_object(os, child); // recursive call

        os << "</object>";
    }

    string vspserver::handle_list(const char* command) {
        string format = "xml";
        vector<string> args = split(command, ',');
        if (args.size() > 1)
            format = to_lower(args[1]);

        if (format != "xml")
            return mkstr("E,unknown hierarchy format '%s'", format.c_str());

        stringstream ss;
        ss << "OK,<?xml version=\"1.0\" ?><hierarchy>";

        for (auto obj : sc_core::sc_get_top_level_objects())
            list_object(ss, obj);

        for (auto tgt : debugging::target::targets())
            ss << "<target>" << tgt->target_name() << "</target>";

        for (auto kbd : ui::keyboard::keyboards())
            ss << "<keyboard>" << kbd->input_name() << "</keyboard>";

        for (auto ptr : ui::pointer::pointers())
            ss << "<pointer>" << ptr->input_name() << "</pointer>";

        for (auto serial : serial::port::all())
            ss << "<serial>" << serial->port_name() << "</serial>";

        for (auto adapter : net::adapter::all())
            ss << "<adapter>" << adapter->adapter_name() << "</adapter>";

        ss << "</hierarchy>";
        return ss.str();
    }

    string vspserver::handle_exec(const char* command) {
        vector<string> args = split(command, ',');

        if (args.size() < 3)
            return mkstr("E,insufficient arguments %d", args.size());

        string name = args[1];
        sc_object* obj = find_object(name);
        if (obj == nullptr)
            return mkstr("E,object '%s' not found", name.c_str());

        module* mod = dynamic_cast<module*>(obj);
        if (mod == nullptr) {
            return mkstr("E,object '%s' does not support commands",
                         name.c_str());
        }

        try {
            stringstream ss;
            vector<string> cmdargs(args.begin() + 3, args.end());
            bool success = mod->execute(args[2], cmdargs, ss);
            return mkstr("%s,%s", success ? "OK" : "E", ss.str().c_str());
        } catch (std::exception& e) {
            return mkstr("E,%s", escape(e.what(), ",").c_str());
        }
    }

    string vspserver::handle_time(const char* command) {
        u64 delta = sc_delta_count();
        u64 nanos = time_to_ns(sc_time_stamp());
        return mkstr("OK,%lu,%lu", nanos, delta);
    }

    string vspserver::handle_rdgq(const char* command) {
        sc_time quantum = tlm::tlm_global_quantum::instance().get();
        return mkstr("OK,%lu", time_to_ns(quantum));
    }

    string vspserver::handle_wrgq(const char* command) {
        vector<string> args = split(command, ',');
        if (args.size() < 2)
            return mkstr("E,insufficient arguments %d", args.size());

        stringstream ss(args[1]);
        u64 t; ss >> t;

        sc_time quantum(t, SC_NS);
        tlm::tlm_global_quantum::instance().set(quantum);

        return "OK";
    }

    string vspserver::handle_geta(const char* command) {
        vector<string> args = split(command, ',');
        if (args.size() < 2)
            return mkstr("E,insufficient arguments %d", args.size());

        string name = args[1];
        sc_attr_base* attr = find_attribute(name);
        if (attr == nullptr)
            return mkstr("E,attribute '%s' not found", name.c_str());

        stringstream ss;
        property_base* prop = dynamic_cast<property_base*>(attr);
        ss << "OK," << (prop ? prop->str() : attr->name());

        return ss.str();
    }

    string vspserver::handle_seta(const char* command) {
        vector<string> args = split(command, ',');
        if (args.size() < 3)
            return mkstr("E,insufficient arguments %d", args.size());

        string name = args[1];
        vector<string> values(args.begin() + 2, args.end());

        sc_attr_base* attr = find_attribute(name);
        if (attr == nullptr)
            return mkstr("E,attribute '%s' not found", name.c_str());

        property_base* prop = dynamic_cast<property_base*>(attr);
        if (prop == nullptr)
            return mkstr("E,attribute '%s' not writable", name.c_str());

        if (values.size() != prop->count()) {
            return mkstr("E,attribute '%s' needs %zu initializers, %zu given",
                         name.c_str(), prop->count(), values.size());
        }

        stringstream ss;
        for (unsigned int i = 0; i < (prop->count() - 1); i++)
            ss << values[i] << property_base::ARRAY_DELIMITER;
        ss << values[prop->count() - 1];
        prop->str(ss.str());

        return "OK";
    }

    string vspserver::handle_quit(const char* command) {
        force_quit();
        return "OK";
    }

    string vspserver::handle_vers(const char* command) {
        stringstream ss;
        ss << "OK,";
        ss << SC_VERSION << ",";
        ss << VCML_VERSION_STRING;
#ifdef VCML_DEBUG
        ss << "-debug";
#endif
        return ss.str();
    }

    void vspserver::resume_simulation(const sc_time& duration) {
        m_duration = duration;
        resume();

        try {
            while (!is_suspending()) {
                int signal = recv_signal(100);
                switch (signal) {
                case 0x0:
                    break;

                case 'u': // update time
                    send_packet(handle_time(nullptr));
                    break;

                case 'x': // quit
                    force_quit();
                    return;

                case 'a': // pause
                    sc_pause();
                    return;

                default:
                    log_debug("received unknown signal 0x%x", signal);
                    break;
                }
            }
        } catch (...) {
            sc_pause();
            disconnect();
        }
    }

    void vspserver::force_quit() {
        stop();
        suspender::quit();

        if (is_connected())
            disconnect();
    }

    vspserver::vspserver(u16 port):
        rspserver(port),
        suspender("vspserver"),
        m_announce(temp_dir() + mkstr("vcml_session_%hu", get_port())) {
        VCML_ERROR_ON(session != nullptr, "vspserver already created");
        session = this;
        atexit(&cleanup_session);

        using std::placeholders::_1;
        register_handler("n", std::bind(&vspserver::handle_none, this, _1));
        register_handler("s", std::bind(&vspserver::handle_step, this, _1));
        register_handler("c", std::bind(&vspserver::handle_cont, this, _1));
        register_handler("l", std::bind(&vspserver::handle_list, this, _1));
        register_handler("e", std::bind(&vspserver::handle_exec, this, _1));
        register_handler("t", std::bind(&vspserver::handle_time, this, _1));
        register_handler("q", std::bind(&vspserver::handle_rdgq, this, _1));
        register_handler("Q", std::bind(&vspserver::handle_wrgq, this, _1));
        register_handler("a", std::bind(&vspserver::handle_geta, this, _1));
        register_handler("A", std::bind(&vspserver::handle_seta, this, _1));
        register_handler("x", std::bind(&vspserver::handle_quit, this, _1));
        register_handler("v", std::bind(&vspserver::handle_vers, this, _1));

        // Create announce file
        ofstream of(m_announce.c_str());
        of << "localhost:" << std::dec << get_port() << ":" << username()
           << ":" << progname() << std::endl;
    }

    vspserver::~vspserver() {
        cleanup();
        session = nullptr;
    }

    void vspserver::start() {
        run_async();
        log_info("vspserver waiting on port %hu", get_port());

        // Finish elaboration first before processing commands
        sc_start(SC_ZERO_TIME);
        suspend();

        while (sim_running()) {
            suspender::handle_requests();
            if (!sim_running())
                break;

            if (m_duration == SC_MAX_TIME)
                sc_start();
            else
                sc_start(m_duration);

            if (sim_running())
                suspend();
        }

        if (is_connected())
            disconnect();
    }

    void vspserver::cleanup() {
        if (!file_exists(m_announce))
            return;

        if (remove(m_announce.c_str())) {
            log_warning("failed to remove file '%s': %s", m_announce.c_str(),
                        strerror(errno));
        }
    }

    void vspserver::handle_connect(const char* peer) {
        log_info("vspserver connected to %s", peer);
    }

    void vspserver::handle_disconnect() {
        if (sim_running())
            log_info("vspserver waiting on port %hu", get_port());
    }

}}
