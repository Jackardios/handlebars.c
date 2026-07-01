/**
 * Copyright (c) anno Domini nostri Jesu Christi MMXVI-MMXXIV John Boehr & contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <yaml.h>

#include "handlebars.h"
#include "handlebars_private.h"
#include "handlebars_memory.h"
#include "handlebars_map.h"
#include "handlebars_stack.h"
#include "handlebars_string.h"
#include "handlebars_value.h"
#include "handlebars_yaml.h"



// Bounds recursion into the YAML document tree. Besides guarding against a
// stack overflow from a deeply nested document, this also breaks the otherwise
// infinite recursion that anchor/alias cycles (e.g. a: &x { b: *x }) produce,
// since libyaml resolves an alias to the very node that contains it.
#ifndef HANDLEBARS_YAML_MAX_DEPTH
#define HANDLEBARS_YAML_MAX_DEPTH 1000
#endif

struct _yaml_ctx {
    yaml_parser_t parser;
    yaml_document_t document;
};

static int _yaml_ctx_dtor(struct _yaml_ctx * holder)
{
    yaml_document_delete(&holder->document);
    yaml_parser_delete(&holder->parser);
    return 0;
}

static void yaml_node_to_value(struct handlebars_context *ctx, struct handlebars_value * value, struct yaml_document_s * document, struct yaml_node_s * node, unsigned int depth)
{
    HANDLEBARS_VALUE_DECL(tmp);
    yaml_node_pair_t * pair;
    yaml_node_item_t * item;
    char * end = NULL;

    if( depth > HANDLEBARS_YAML_MAX_DEPTH ) {
        handlebars_throw(ctx, HANDLEBARS_ERROR, "Maximum YAML nesting depth exceeded");
    }

    switch( node->type ) {
        case YAML_MAPPING_NODE: {
            struct handlebars_map * map = handlebars_map_ctor(ctx, node->data.mapping.pairs.top - node->data.mapping.pairs.start);
            for( pair = node->data.mapping.pairs.start; pair < node->data.mapping.pairs.top; pair++ ) {
                yaml_node_t * keyNode = yaml_document_get_node(document, pair->key);
                yaml_node_t * valueNode = yaml_document_get_node(document, pair->value);
                assert(keyNode->type == YAML_SCALAR_NODE);
                yaml_node_to_value(ctx, tmp, document, valueNode, depth + 1);
                map = handlebars_map_str_update(map, (const char *) keyNode->data.scalar.value, keyNode->data.scalar.length, tmp);
            }
            handlebars_value_map(value, map);
            break;
        }
        case YAML_SEQUENCE_NODE: {
            struct handlebars_stack * stack = handlebars_stack_ctor(ctx, node->data.sequence.items.top - node->data.sequence.items.start);
            for( item = node->data.sequence.items.start; item < node->data.sequence.items.top; item++) {
                yaml_node_t * valueNode = yaml_document_get_node(document, *item);
                yaml_node_to_value(ctx, tmp, document, valueNode, depth + 1);
                stack = handlebars_stack_push(stack, tmp);
            }
            handlebars_value_array(value, stack);
            break;
        }
        case YAML_SCALAR_NODE:
            if( 0 == strcmp((const char *) node->data.scalar.value, "true") ) {
                handlebars_value_boolean(value, 1);
            } else if( 0 == strcmp((const char *) node->data.scalar.value, "false") ) {
                handlebars_value_boolean(value, 0);
            } else {
                long lval;
                double dval;
                // Long
                lval = strtol((const char *) node->data.scalar.value, &end, 10);
                if( !*end ) {
                    handlebars_value_integer(value, lval);
                    goto done;
                }
                // Double
                dval = strtod((const char *) node->data.scalar.value, &end);
                if( !*end ) {
                    handlebars_value_float(value, dval);
                    goto done;
                }
                // String
                handlebars_value_str(value, handlebars_string_ctor(ctx, (const char *) node->data.scalar.value, node->data.scalar.length));
            }
            break;
        default:
            // ruh roh
            assert(0);
            break;
    }

done:
    HANDLEBARS_VALUE_UNDECL(tmp);
}

void handlebars_value_init_yaml_node(struct handlebars_context *ctx, struct handlebars_value * value, struct yaml_document_s * document, struct yaml_node_s * node)
{
    yaml_node_to_value(ctx, value, document, node, 0);
}

void handlebars_value_init_yaml_string(struct handlebars_context * ctx, struct handlebars_value * value, const char * yaml)
{
    struct _yaml_ctx * yctx = handlebars_talloc_zero(ctx, struct _yaml_ctx);
    HANDLEBARS_MEMCHECK(yctx, ctx);
    talloc_set_destructor(yctx, _yaml_ctx_dtor);
    yaml_parser_initialize(&yctx->parser);
    yaml_parser_set_input_string(&yctx->parser, (unsigned char *) yaml, strlen(yaml));
    yaml_parser_load(&yctx->parser, &yctx->document);
    yaml_node_t * node = yaml_document_get_root_node(&yctx->document);
    if( node ) {
        handlebars_value_init_yaml_node(ctx, value, &yctx->document, node);
    } else {
        handlebars_throw(ctx, HANDLEBARS_ERROR, "YAML Parse Error: [%d] %s", yctx->parser.error, yctx->parser.problem);
    }
    handlebars_talloc_free(yctx);
}
