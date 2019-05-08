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

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace ::testing;

#include "vcml.h"

using ::vcml::u32;

class mock_peripheral_base {
public:
    virtual ~mock_peripheral_base() {}
    virtual u32 reg_read() = 0;
    virtual u32 reg_write(u32) = 0;
};

class mock_peripheral: public vcml::peripheral, public mock_peripheral_base {
public:
    vcml::reg<mock_peripheral, u32> test_reg_a;
    vcml::reg<mock_peripheral, u32> test_reg_b;

    u32 _reg_read() { return reg_read(); }
    u32 _reg_write(u32 val) { return reg_write(val); }

    mock_peripheral(const sc_core::sc_module_name& nm = "mock_peripheral"):
        vcml::peripheral(nm),
        mock_peripheral_base(),
        test_reg_a("test_reg_a", 0x0, 0xffffffff),
        test_reg_b("test_reg_b", 0x4, 0xffffffff) {
        test_reg_b.allow_read_write();
        test_reg_b.read = &mock_peripheral::_reg_read;
        test_reg_b.write = &mock_peripheral::_reg_write;
    }

    MOCK_METHOD0(reg_read, u32());
    MOCK_METHOD1(reg_write, u32(u32));
};

TEST(registers, read) {
    mock_peripheral mock;

    mock.read_latency = sc_core::sc_time(1, sc_core::SC_US);
    mock.write_latency = sc_core::sc_time(10, sc_core::SC_US);

    sc_core::sc_time t;
    tlm::tlm_generic_payload tx;
    unsigned char buffer [] = { 0xcc, 0xcc, 0xcc, 0xcc };
    unsigned char expect [] = { 0x37, 0x13, 0x00, 0x00 };

    mock.test_reg_a = 0x1337;
    t = sc_core::SC_ZERO_TIME;
    vcml::tx_setup(tx, tlm::TLM_READ_COMMAND, 0, buffer, sizeof(buffer));

    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 4);
    EXPECT_EQ(mock.test_reg_a, 0x00001337);
    EXPECT_EQ(mock.test_reg_b, 0xffffffff);
    EXPECT_EQ(buffer[0], expect[0]);
    EXPECT_EQ(buffer[1], expect[1]);
    EXPECT_EQ(buffer[2], expect[2]);
    EXPECT_EQ(buffer[3], expect[3]);
    EXPECT_EQ(t, mock.read_latency);
    EXPECT_TRUE(tx.is_response_ok());
}


TEST(registers, read_callback) {
    mock_peripheral mock;

    mock.read_latency = sc_core::sc_time(1, sc_core::SC_US);
    mock.write_latency = sc_core::sc_time(10, sc_core::SC_US);

    sc_core::sc_time t;
    tlm::tlm_generic_payload tx;
    unsigned char buffer [] = { 0xcc, 0xcc, 0xcc, 0xcc };
    unsigned char expect [] = { 0x37, 0x13, 0x00, 0x00 };

    mock.test_reg_b = 0x1337;
    t = sc_core::SC_ZERO_TIME;
    vcml::tx_setup(tx, tlm::TLM_READ_COMMAND, 4, buffer, sizeof(buffer));


    EXPECT_CALL(mock, reg_read()).WillOnce(Return(mock.test_reg_b.get()));
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 4);
    EXPECT_EQ(mock.test_reg_a, 0xffffffff);
    EXPECT_EQ(mock.test_reg_b, 0x00001337);
    EXPECT_EQ(buffer[0], expect[0]);
    EXPECT_EQ(buffer[1], expect[1]);
    EXPECT_EQ(buffer[2], expect[2]);
    EXPECT_EQ(buffer[3], expect[3]);
    EXPECT_EQ(t, mock.read_latency);
    EXPECT_TRUE(tx.is_response_ok());
}

TEST(registers, write) {
    mock_peripheral mock;

    mock.read_latency = sc_core::sc_time(1, sc_core::SC_US);
    mock.write_latency = sc_core::sc_time(10, sc_core::SC_US);

    sc_core::sc_time t;
    tlm::tlm_generic_payload tx;
    unsigned char buffer [] = { 0x11, 0x22, 0x33, 0x44 };

    t = sc_core::SC_ZERO_TIME;
    vcml::tx_setup(tx, tlm::TLM_WRITE_COMMAND, 0, buffer, sizeof(buffer));

    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 4);
    EXPECT_EQ(mock.test_reg_a, 0x44332211);
    EXPECT_EQ(mock.test_reg_b, 0xffffffff);
    EXPECT_EQ(t, mock.write_latency);
    EXPECT_TRUE(tx.is_response_ok());
}

TEST(registers, write_callback) {
    mock_peripheral mock;

    mock.read_latency = sc_core::sc_time(1, sc_core::SC_US);
    mock.write_latency = sc_core::sc_time(10, sc_core::SC_US);

    sc_core::sc_time t;
    tlm::tlm_generic_payload tx;
    u32 value = 0x98765432;
    unsigned char buffer [] = { 0x11, 0x22, 0x33, 0x44 };

    t = sc_core::SC_ZERO_TIME;
    vcml::tx_setup(tx, tlm::TLM_WRITE_COMMAND, 4, buffer, sizeof(buffer));

    EXPECT_CALL(mock, reg_write(0x44332211)).WillOnce(Return(value));
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 4);
    EXPECT_EQ(mock.test_reg_a, 0xffffffff);
    EXPECT_EQ(mock.test_reg_b, value);
    EXPECT_EQ(t, mock.write_latency);
    EXPECT_TRUE(tx.is_response_ok());
}

TEST(registers, read_byte_enable) {
    mock_peripheral mock;

    mock.read_latency = sc_core::sc_time(1, sc_core::SC_US);
    mock.write_latency = sc_core::sc_time(10, sc_core::SC_US);

    sc_core::sc_time t;
    tlm::tlm_generic_payload tx;
    unsigned char buffer [] = { 0xcc, 0xcc, 0x00, 0x00 };
    unsigned char bebuff [] = { 0xff, 0xff, 0x00, 0x00 };
    unsigned char expect [] = { 0x37, 0x13, 0x00, 0x00 };

    mock.test_reg_a = 0x1337;
    t = sc_core::SC_ZERO_TIME;
    vcml::tx_setup(tx, tlm::TLM_READ_COMMAND, 0, buffer, sizeof(buffer));
    tx.set_byte_enable_ptr(bebuff);
    tx.set_byte_enable_length(sizeof(bebuff));

    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 2);
    EXPECT_EQ(mock.test_reg_a, 0x00001337);
    EXPECT_EQ(mock.test_reg_b, 0xffffffff);
    EXPECT_EQ(buffer[0], expect[0]);
    EXPECT_EQ(buffer[1], expect[1]);
    EXPECT_EQ(buffer[2], expect[2]);
    EXPECT_EQ(buffer[3], expect[3]);
    EXPECT_EQ(t, mock.read_latency);
    EXPECT_TRUE(tx.is_response_ok());
}

TEST(registers, write_byte_enable) {
    mock_peripheral mock;

    mock.read_latency = sc_core::sc_time(1, sc_core::SC_US);
    mock.write_latency = sc_core::sc_time(10, sc_core::SC_US);

    sc_core::sc_time t;
    tlm::tlm_generic_payload tx;
    unsigned char buffer [] = { 0x11, 0x22, 0x33, 0x44 };
    unsigned char bebuff [] = { 0xff, 0x00, 0xff, 0x00 };

    mock.test_reg_a = 0;
    t = sc_core::SC_ZERO_TIME;
    vcml::tx_setup(tx, tlm::TLM_WRITE_COMMAND, 0, buffer, sizeof(buffer));
    tx.set_byte_enable_ptr(bebuff);
    tx.set_byte_enable_length(sizeof(bebuff));

    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 2);
    EXPECT_EQ(mock.test_reg_a, 0x00330011);
    EXPECT_EQ(mock.test_reg_b, 0xffffffff);
    EXPECT_EQ(t, mock.write_latency);
    EXPECT_TRUE(tx.is_response_ok());
}

TEST(registers, permissions) {
    mock_peripheral mock;

    mock.read_latency = sc_core::sc_time(1, sc_core::SC_US);
    mock.write_latency = sc_core::sc_time(10, sc_core::SC_US);

    sc_core::sc_time t;
    tlm::tlm_generic_payload tx;
    unsigned char buffer [] = { 0x11, 0x22, 0x33, 0x44 };

    t = sc_core::SC_ZERO_TIME;
    mock.test_reg_b.allow_read();
    vcml::tx_setup(tx, tlm::TLM_WRITE_COMMAND, 4, buffer, sizeof(buffer));

    EXPECT_CALL(mock, reg_write(_)).Times(0);
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 0);
    EXPECT_EQ(tx.get_response_status(), tlm::TLM_COMMAND_ERROR_RESPONSE);
    EXPECT_EQ(mock.test_reg_a, 0xffffffff);
    EXPECT_EQ(mock.test_reg_b, 0xffffffff);
    EXPECT_EQ(t, mock.write_latency);

    t = sc_core::SC_ZERO_TIME;
    mock.test_reg_b.allow_write();
    vcml::tx_setup(tx, tlm::TLM_READ_COMMAND, 4, buffer, sizeof(buffer));

    EXPECT_CALL(mock, reg_read()).Times(0);
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 0);
    EXPECT_EQ(tx.get_response_status(), tlm::TLM_COMMAND_ERROR_RESPONSE);
    EXPECT_EQ(mock.test_reg_a, 0xffffffff);
    EXPECT_EQ(mock.test_reg_b, 0xffffffff);
    EXPECT_EQ(t, mock.read_latency);
}

TEST(registers, misaligned_accesses) {
    mock_peripheral mock;

    mock.read_latency = sc_core::sc_time(1, sc_core::SC_US);
    mock.write_latency = sc_core::sc_time(10, sc_core::SC_US);

    sc_core::sc_time t;
    tlm::tlm_generic_payload tx;
    unsigned char buffer [] = { 0x11, 0x22, 0x33, 0x44 };

    mock.test_reg_a = 0;
    t = sc_core::SC_ZERO_TIME;
    vcml::tx_setup(tx, tlm::TLM_WRITE_COMMAND, 1, buffer, 2);

    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 2);
    EXPECT_EQ(mock.test_reg_a, 0x00221100);
    EXPECT_EQ(mock.test_reg_b, 0xffffffff);
    EXPECT_EQ(t, mock.write_latency);
    EXPECT_TRUE(tx.is_response_ok());

    t = sc_core::SC_ZERO_TIME;
    vcml::tx_setup(tx, tlm::TLM_WRITE_COMMAND, 1, buffer, 4);

    EXPECT_CALL(mock, reg_write(0xffffff44)).WillOnce(Return(0xffffff44));
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 4); // !
    EXPECT_EQ(mock.test_reg_a, 0x33221100);
    EXPECT_EQ(mock.test_reg_b, 0xffffff44); // !
    EXPECT_EQ(t, mock.write_latency);
    EXPECT_TRUE(tx.is_response_ok());

    unsigned char largebuf [8] = { 0xff };
    t = sc_core::SC_ZERO_TIME;
    vcml::tx_setup(tx, tlm::TLM_READ_COMMAND, 0, largebuf, 8);

    EXPECT_CALL(mock, reg_read()).WillOnce(Return(mock.test_reg_b.get()));
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 8);
    EXPECT_EQ(largebuf[0], 0x00);
    EXPECT_EQ(largebuf[1], 0x11);
    EXPECT_EQ(largebuf[2], 0x22);
    EXPECT_EQ(largebuf[3], 0x33);
    EXPECT_EQ(largebuf[4], 0x44);
    EXPECT_EQ(largebuf[5], 0xff);
    EXPECT_EQ(largebuf[6], 0xff);
    EXPECT_EQ(largebuf[7], 0xff);
    EXPECT_EQ(t, mock.read_latency);
    EXPECT_TRUE(tx.is_response_ok());
}

TEST(registers, banking) {
    mock_peripheral mock;
    mock.test_reg_a.set_banked();

    mock.read_latency = sc_core::sc_time(1, sc_core::SC_US);
    mock.write_latency = sc_core::sc_time(10, sc_core::SC_US);

    sc_core::sc_time t;
    tlm::tlm_generic_payload tx;
    vcml::ext_bank bank;
    const vcml::u8 val1 = 0xab;
    const vcml::u8 val2 = 0xcd;
    unsigned char buffer;

    tx.set_extension(&bank);

    buffer = val1;
    bank.set_bank(1);
    vcml::tx_setup(tx, tlm::TLM_WRITE_COMMAND, 0, &buffer, 1);
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 1);
    EXPECT_TRUE(tx.is_response_ok());

    buffer = val2;
    bank.set_bank(2);
    vcml::tx_setup(tx, tlm::TLM_WRITE_COMMAND, 0, &buffer, 1);
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 1);
    EXPECT_TRUE(tx.is_response_ok());

    buffer = 0x0;
    bank.set_bank(1);
    vcml::tx_setup(tx, tlm::TLM_READ_COMMAND, 0, &buffer, 1);
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 1);
    EXPECT_TRUE(tx.is_response_ok());
    EXPECT_EQ(buffer, val1);

    buffer = 0x0;
    bank.set_bank(2);
    vcml::tx_setup(tx, tlm::TLM_READ_COMMAND, 0, &buffer, 1);
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 1);
    EXPECT_TRUE(tx.is_response_ok());
    EXPECT_EQ(buffer, val2);

    tx.clear_extension(&bank);
}

TEST(registers, endianess) {
    mock_peripheral mock;

    mock.set_big_endian();
    mock.read_latency = sc_core::sc_time(1, sc_core::SC_US);
    mock.write_latency = sc_core::sc_time(10, sc_core::SC_US);

    sc_core::sc_time t;
    tlm::tlm_generic_payload tx;
    u32 buffer = 0;

    mock.test_reg_a = 0x11223344;
    vcml::tx_setup(tx, tlm::TLM_READ_COMMAND, 0, &buffer, 4);
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 4);
    EXPECT_EQ(buffer, 0x44332211);
    EXPECT_EQ(t, mock.read_latency);
    EXPECT_TRUE(tx.is_response_ok());

    buffer = 0xeeff00cc;
    t = sc_core::SC_ZERO_TIME;
    vcml::tx_setup(tx, tlm::TLM_WRITE_COMMAND, 0, &buffer, 4);
    EXPECT_EQ(mock.transport(tx, t, vcml::VCML_FLAG_NONE), 4);
    EXPECT_EQ(mock.test_reg_a, 0xcc00ffee);
    EXPECT_EQ(t, mock.write_latency);
    EXPECT_TRUE(tx.is_response_ok());
}
