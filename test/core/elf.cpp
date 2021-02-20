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

#include "testing.h"

TEST(elf, main) {
    ASSERT_GE(args.size(), 2);
    string path = args[1] + "/elf.elf";
    ASSERT_TRUE(file_exists(path));

    vcml::elf elf(path);
    EXPECT_EQ(elf.get_filename(), path);
    EXPECT_EQ(elf.get_entry_point(), 0x24e0);
    EXPECT_EQ(elf.get_endianess(), ENDIAN_BIG);

    EXPECT_FALSE(elf.is_64bit());
}

TEST(elf, sections) {
    ASSERT_GE(args.size(), 2);
    string path = args[1] + "/elf.elf";
    ASSERT_TRUE(vcml::file_exists(path));

    vcml::elf elf(path);
    EXPECT_EQ(elf.get_filename(), path);
    EXPECT_EQ(elf.get_entry_point(), 0x24e0);
    EXPECT_EQ(elf.get_endianess(), ENDIAN_BIG);

    EXPECT_FALSE(elf.is_64bit());
    EXPECT_FALSE(elf.get_sections().empty());
    EXPECT_EQ(elf.get_num_sections(), 30);

    EXPECT_NE(elf.get_section(".ctors"), nullptr);
    EXPECT_NE(elf.get_section(".text"), nullptr);
    EXPECT_NE(elf.get_section(".data"), nullptr);
    EXPECT_NE(elf.get_section(".bss"), nullptr);
    EXPECT_NE(elf.get_section(".init"), nullptr);
    EXPECT_NE(elf.get_section(".symtab"), nullptr);
    EXPECT_NE(elf.get_section(".strtab"), nullptr);

    elf_section* text = elf.get_section(".text");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->get_name(), ".text");
    EXPECT_TRUE(text->is_executable());
    EXPECT_TRUE(text->needs_alloc());
    EXPECT_FALSE(text->is_writeable());

    EXPECT_EQ(text->get_virt_addr(), 0x233c);
    EXPECT_EQ(text->get_size(), 0x47c);

    EXPECT_FALSE(text->contains(0x233b));
    EXPECT_TRUE(text->contains(0x233c));
    EXPECT_TRUE(text->contains(0x27b7));
    EXPECT_FALSE(text->contains(0x27b8));
}

TEST(elf, symbols) {
    ASSERT_GE(args.size(), 2);
    string path = args[1] + "/elf.elf";
    ASSERT_TRUE(file_exists(path));

    vcml::elf elf(path);
    EXPECT_FALSE(elf.get_symbols().empty());
    EXPECT_EQ(elf.get_num_symbols(), 71);

    elf_symbol* main = elf.get_symbol("main");
    ASSERT_NE(main, nullptr);
    EXPECT_EQ(main->get_name(), "main");
    EXPECT_EQ(main->get_type(), ELF_SYM_FUNCTION);
    EXPECT_EQ(main->get_virt_addr(), 0x233c);

    vcml::elf_symbol* ctors = elf.get_symbol("__CTOR_LIST__");
    ASSERT_NE(ctors, nullptr);
    EXPECT_EQ(ctors->get_name(), "__CTOR_LIST__");
    EXPECT_EQ(ctors->get_type(), ELF_SYM_OBJECT);
    EXPECT_EQ(ctors->get_virt_addr(), 0x4860);
}
