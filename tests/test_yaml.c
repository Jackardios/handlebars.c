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
#include <stdio.h>
#include <talloc.h>

#include "handlebars_memory.h"
#include "handlebars_string.h"
#include "handlebars_value.h"
#include "handlebars_yaml.h"
#include "utils.h"


START_TEST(test_boolean_yaml_true)
{
    HANDLEBARS_VALUE_DECL(value);
    handlebars_value_init_yaml_string(context, value, "---\ntrue");
    ck_assert_ptr_ne(value, NULL);
    ck_assert_int_eq(handlebars_value_get_type(value), HANDLEBARS_VALUE_TYPE_TRUE);
    ck_assert_int_eq(handlebars_value_get_boolval(value), 1);
    HANDLEBARS_VALUE_UNDECL(value);
    ASSERT_INIT_BLOCKS();
}
END_TEST

START_TEST(test_boolean_yaml_false)
{
    HANDLEBARS_VALUE_DECL(value);
    handlebars_value_init_yaml_string(context, value, "---\nfalse");
    ck_assert_ptr_ne(value, NULL);
    ck_assert_int_eq(handlebars_value_get_type(value), HANDLEBARS_VALUE_TYPE_FALSE);
    ck_assert_int_eq(handlebars_value_get_boolval(value), 0);
    HANDLEBARS_VALUE_UNDECL(value);
    ASSERT_INIT_BLOCKS();
}
END_TEST

START_TEST(test_int_yaml)
{
    HANDLEBARS_VALUE_DECL(value);
    handlebars_value_init_yaml_string(context, value, "---\n2358");
    ck_assert_ptr_ne(value, NULL);
    ck_assert_int_eq(handlebars_value_get_type(value), HANDLEBARS_VALUE_TYPE_INTEGER);
    ck_assert_int_eq(handlebars_value_get_intval(value), 2358);
    HANDLEBARS_VALUE_UNDECL(value);
    ASSERT_INIT_BLOCKS();
}
END_TEST

START_TEST(test_float_yaml)
{
    HANDLEBARS_VALUE_DECL(value);
    handlebars_value_init_yaml_string(context, value, "---\n1234.4321");
    ck_assert_ptr_ne(value, NULL);
    ck_assert_int_eq(handlebars_value_get_type(value), HANDLEBARS_VALUE_TYPE_FLOAT);
    // Note: converting to int - precision issue
    ck_assert_int_eq(handlebars_value_get_floatval(value), 1234.4321);
    HANDLEBARS_VALUE_UNDECL(value);
    ASSERT_INIT_BLOCKS();
}
END_TEST

START_TEST(test_string_yaml)
{
    HANDLEBARS_VALUE_DECL(value);
	handlebars_value_init_yaml_string(context, value, "---\n\"test\"");
	ck_assert_ptr_ne(value, NULL);
	ck_assert_int_eq(handlebars_value_get_type(value), HANDLEBARS_VALUE_TYPE_STRING);
    const char * tmp = handlebars_value_get_strval(value);
	ck_assert_str_eq(tmp, "test");
	ck_assert_int_eq(handlebars_value_get_strlen(value), 4);
    HANDLEBARS_VALUE_UNDECL(value);
    ASSERT_INIT_BLOCKS();
}
END_TEST

START_TEST(test_parse_error_yaml)
{
    HANDLEBARS_VALUE_DECL(value);
    jmp_buf buf;

    if( handlebars_setjmp_ex(context, &buf) ) {
        char * error = NULL;
        if( 0 != regex_compare("^YAML Parse Error", handlebars_error_msg(context), &error) ) {
            ck_abort_msg("%s", error);
        }
        return;
    }

    handlebars_value_init_yaml_string(context, value, "---\n'");
    (void) value;
    ck_assert_msg(0, "Parse error should have longjmp'd");

    HANDLEBARS_VALUE_UNDECL(value);
}
END_TEST

// Regression (C2): converting a YAML document into a value recurses into nested
// mappings/sequences. A deeply nested document (or an anchor/alias cycle, which
// libyaml resolves into a self-referential node graph) would otherwise recurse
// until the C stack overflows. The conversion now caps its depth and raises a
// clean error.
START_TEST(test_deep_nesting_yaml)
{
    HANDLEBARS_VALUE_DECL(value);
    jmp_buf buf;
    struct handlebars_string * yaml;
    int i;
    const int depth = 2000; // > HANDLEBARS_YAML_MAX_DEPTH (1000)

    if( handlebars_setjmp_ex(context, &buf) ) {
        fprintf(stderr, "Got expected error: %s\n", handlebars_error_message(context));
        ck_assert(1);
        HANDLEBARS_VALUE_UNDECL(value);
        return;
    }

    // Flow-style nested sequences: [[[ ... ]]]
    yaml = handlebars_string_ctor(context, HBS_STRL("---\n"));
    for( i = 0; i < depth; i++ ) {
        yaml = handlebars_string_append(context, yaml, HBS_STRL("["));
    }
    for( i = 0; i < depth; i++ ) {
        yaml = handlebars_string_append(context, yaml, HBS_STRL("]"));
    }

    handlebars_value_init_yaml_string(context, value, hbs_str_val(yaml));
    ck_abort_msg("Expected a nesting-depth error for deeply nested YAML");

    HANDLEBARS_VALUE_UNDECL(value);
}
END_TEST

static Suite * suite(void);
static Suite * suite(void)
{
    Suite * s = suite_create("YAML");

    REGISTER_TEST_FIXTURE(s, test_boolean_yaml_true, "Boolean - true");
    REGISTER_TEST_FIXTURE(s, test_boolean_yaml_false, "Boolean - false");
    REGISTER_TEST_FIXTURE(s, test_int_yaml, "Integer");
    REGISTER_TEST_FIXTURE(s, test_float_yaml, "Float");
    REGISTER_TEST_FIXTURE(s, test_string_yaml, "String");
    REGISTER_TEST_FIXTURE(s, test_parse_error_yaml, "YAML Parse Error");
    REGISTER_TEST_FIXTURE(s, test_deep_nesting_yaml, "YAML deep nesting raises error");

    return s;
}

int main(void)
{
    return default_main(&suite);
}
