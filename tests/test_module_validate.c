/**
 * Copyright (c) anno Domini nostri Jesu Christi MMXVI-MMXXIV John Boehr & contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <check.h>
#include <stddef.h>
#include <string.h>
#include <talloc.h>

#define HANDLEBARS_OPCODES_PRIVATE
#define HANDLEBARS_OPCODE_SERIALIZER_PRIVATE

#include "handlebars.h"
#include "handlebars_compiler.h"
#include "handlebars_opcode_serializer.h"
#include "handlebars_opcodes.h"
#include "handlebars_parser.h"
#include "handlebars_string.h"
#include "utils.h"



static struct handlebars_module * make_module(const char * tmpl)
{
    struct handlebars_ast_node * ast = handlebars_parse_ex(parser, handlebars_string_ctor(context, tmpl, strlen(tmpl)), 0);
    struct handlebars_program * program = handlebars_compiler_compile_ex(compiler, ast);
    return handlebars_program_serialize(context, program);
}

// A freshly serialized module must validate against its own declared size.
START_TEST(test_module_validate_accepts_valid)
{
    struct handlebars_module * m = make_module("{{foo}} {{#bar}}{{baz}}{{/bar}}");
    ck_assert(handlebars_module_validate(m, m->size, NULL));
}
END_TEST

// Each independent corruption of the serialized module must be rejected, and
// the module must validate again once the field is restored (proving the check
// is specific to the corruption).
START_TEST(test_module_validate_rejects_corruption)
{
    struct handlebars_module * m = make_module("{{foo}} {{#bar}}{{baz}}{{/bar}}");
    size_t good_size = m->size;

    ck_assert(handlebars_module_validate(m, good_size, NULL));

    // Buffer too small to even contain the fixed header
    ck_assert(!handlebars_module_validate(m, sizeof(struct handlebars_module) - 1, NULL));

    // Declared size larger than the buffer we claim to back it with
    ck_assert(!handlebars_module_validate(m, good_size - 1, NULL));

    // Corrupt magic
    unsigned char h0 = m->header[0];
    m->header[0] = (unsigned char) (h0 ^ 0xff);
    ck_assert(!handlebars_module_validate(m, good_size, NULL));
    m->header[0] = h0;
    ck_assert(handlebars_module_validate(m, good_size, NULL));

    // Corrupt version
    int ver = m->version;
    m->version = ver + 1;
    ck_assert(!handlebars_module_validate(m, good_size, NULL));
    m->version = ver;
    ck_assert(handlebars_module_validate(m, good_size, NULL));

    // Absurd opcode_count that cannot fit in the buffer
    size_t oc = m->opcode_count;
    m->opcode_count = (size_t) -1;
    ck_assert(!handlebars_module_validate(m, good_size, NULL));
    m->opcode_count = oc;

    // Absurd program_count that cannot fit in the buffer
    size_t pc = m->program_count;
    m->program_count = (size_t) -1;
    ck_assert(!handlebars_module_validate(m, good_size, NULL));
    m->program_count = pc;
    ck_assert(handlebars_module_validate(m, good_size, NULL));

    // A program whose opcode window runs past the opcode array
    ck_assert(m->program_count > 0);
    size_t off = m->programs[0].opcode_offset;
    m->programs[0].opcode_offset = m->opcode_count + 1;
    ck_assert(!handlebars_module_validate(m, good_size, NULL));
    m->programs[0].opcode_offset = off;
    ck_assert(handlebars_module_validate(m, good_size, NULL));
}
END_TEST

static Suite * suite(void);
static Suite * suite(void)
{
    Suite * s = suite_create("Module Validate");
    REGISTER_TEST_FIXTURE(s, test_module_validate_accepts_valid, "Valid module is accepted");
    REGISTER_TEST_FIXTURE(s, test_module_validate_rejects_corruption, "Corrupt module is rejected");
    return s;
}

int main(void)
{
    return default_main(&suite);
}
