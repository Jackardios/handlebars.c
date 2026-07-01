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
#include <setjmp.h>
#include <string.h>
#include <talloc.h>

#include "handlebars.h"
#include "handlebars_ast.h"
#include "handlebars_compiler.h"
#include "handlebars_delimiters.h"
#include "handlebars_parser.h"
#include "handlebars_string.h"
#include "handlebars_memory.h"
#include "utils.h"



// Regression: a short comment such as {{!-}} or {{~!-}} used to make
// handlebars_ast_helper_strip_comment produce start > end, underflowing the
// size_t length in handlebars_string_truncate -> memmove(SIZE_MAX) -> SIGSEGV.
// After the fix these parse cleanly as empty comments.
START_TEST(test_parse_short_comment)
{
    struct handlebars_ast_node * node;

    node = handlebars_parse_ex(parser, handlebars_string_ctor(context, HBS_STRL("{{!-}}")), 0);
    ck_assert_ptr_ne(NULL, node);

    node = handlebars_parse_ex(parser, handlebars_string_ctor(context, HBS_STRL("{{~!-}}")), 0);
    ck_assert_ptr_ne(NULL, node);
}
END_TEST

// Regression: a set-delimiters tag with an empty close delimiter, e.g. {{=a =}},
// used to compute a negative close-delimiter length that wrapped to a huge
// size_t in handlebars_string_ctor -> memcpy(SIZE_MAX) -> crash. It must now
// raise a clean parse error instead.
START_TEST(test_delimiter_empty_close)
{
    jmp_buf buf;

    if( handlebars_setjmp_ex(context, &buf) ) {
        fprintf(stderr, "Got expected error: %s\n", handlebars_error_message(context));
        ck_assert(1);
        return;
    }

    (void) handlebars_preprocess_delimiters(
        context,
        handlebars_string_ctor(context, HBS_STRL("{{=a =}}")),
        NULL,
        NULL
    );
    ck_abort_msg("Expected an error for an empty close delimiter");
}
END_TEST

// Regression: deeply nested blocks used to write one element past the fixed
// 64-slot compiler source-node/block-param stacks (off-by-one in the bounds
// check). Compilation must now raise a clean stack-overflow error, not corrupt
// memory.
START_TEST(test_compiler_deep_nesting)
{
    jmp_buf buf;
    struct handlebars_string * tmpl;
    int i;
    const int depth = 100; // > HANDLEBARS_COMPILER_STACK_SIZE (64)

    if( handlebars_setjmp_ex(context, &buf) ) {
        fprintf(stderr, "Got expected error: %s\n", handlebars_error_message(context));
        ck_assert(1);
        return;
    }

    tmpl = handlebars_string_ctor(context, HBS_STRL(""));
    for( i = 0; i < depth; i++ ) {
        tmpl = handlebars_string_append(context, tmpl, HBS_STRL("{{#a}}"));
    }
    for( i = 0; i < depth; i++ ) {
        tmpl = handlebars_string_append(context, tmpl, HBS_STRL("{{/a}}"));
    }

    struct handlebars_ast_node * ast = handlebars_parse_ex(parser, tmpl, 0);
    ck_assert_ptr_ne(NULL, ast);
    (void) handlebars_compiler_compile_ex(compiler, ast);
    ck_abort_msg("Expected a stack-overflow error for deeply nested blocks");
}
END_TEST

// Regression (C3): parsing runs the whitespace tree-walk over the freshly built
// AST before compilation. Without a bound, a pathologically nested template can
// recurse deep enough to overflow the C stack in that walk (and later ones).
// The walk now caps its depth and raises a clean parse error, so even a template
// far deeper than anything the compiler would accept cannot crash the parser.
START_TEST(test_parse_deep_nesting)
{
    jmp_buf buf;
    struct handlebars_string * tmpl;
    int i;
    const int depth = 2000; // >> HANDLEBARS_WHITESPACE_MAX_DEPTH once doubled per level

    if( handlebars_setjmp_ex(context, &buf) ) {
        fprintf(stderr, "Got expected error: %s\n", handlebars_error_message(context));
        ck_assert(1);
        return;
    }

    tmpl = handlebars_string_ctor(context, HBS_STRL(""));
    for( i = 0; i < depth; i++ ) {
        tmpl = handlebars_string_append(context, tmpl, HBS_STRL("{{#a}}"));
    }
    for( i = 0; i < depth; i++ ) {
        tmpl = handlebars_string_append(context, tmpl, HBS_STRL("{{/a}}"));
    }

    (void) handlebars_parse_ex(parser, tmpl, 0);
    ck_abort_msg("Expected a nesting-depth error while parsing deeply nested blocks");
}
END_TEST

static Suite * suite(void);
static Suite * suite(void)
{
    Suite * s = suite_create("Parser Errors");
    REGISTER_TEST_FIXTURE(s, test_parse_short_comment, "Short comment ({{!-}}) does not crash");
    REGISTER_TEST_FIXTURE(s, test_delimiter_empty_close, "Empty close delimiter raises error");
    REGISTER_TEST_FIXTURE(s, test_compiler_deep_nesting, "Deeply nested blocks raise stack overflow");
    REGISTER_TEST_FIXTURE(s, test_parse_deep_nesting, "Deeply nested blocks do not crash the parser");
    return s;
}

int main(void)
{
    return default_main(&suite);
}
