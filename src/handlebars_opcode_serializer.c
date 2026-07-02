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

#include <assert.h>
#include <string.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#define HANDLEBARS_COMPILER_PRIVATE
#define HANDLEBARS_OPCODE_SERIALIZER_PRIVATE
#define HANDLEBARS_OPCODES_PRIVATE

#include "handlebars.h"
#include "handlebars_memory.h"
#include "handlebars_private.h"

#include "handlebars_compiler.h"
#include "handlebars_opcodes.h"
#include "handlebars_opcode_serializer.h"
#include "handlebars_string.h"

#define PATCH(ptr, baseaddr) ptr = (void *) (((char *) ptr) - ((char *) module->addr) + ((char *) baseaddr))
#define align_size(size) handlebars_align_size(size, sizeof(void *))

const size_t HANDLEBARS_MODULE_SIZE = sizeof(struct handlebars_module);
const size_t HANDLEBARS_MODULE_TABLE_ENTRY_SIZE = sizeof(struct handlebars_module_table_entry);

static void * append(struct handlebars_module * module, void * source, size_t size)
{
    size_t aligned_size = align_size(size);
    void * addr = &module->data[module->data_offset];
#ifdef HANDLEBARS_ENABLE_DEBUG
    if (NULL != getenv("HANDLEBARS_OPCODE_SERIALIZE_DEBUG")) {
        fprintf(stderr, "Data offset: %zu, Append size: %zu, Buffer size: %zu, Aligned size: %zu\n", module->data_offset, size, module->size, aligned_size);
    }
    assert(module->data_offset < module->size - sizeof(struct handlebars_module));
    assert(((uintptr_t) addr) % sizeof(void *) == 0);
#endif
    memcpy(addr, source, size);
    if (aligned_size != size) {
        memset((char *) addr + size, 0, aligned_size - size);
    }
    module->data_offset += aligned_size;
    return addr;
}

static inline void patch_string(struct handlebars_string * str) {
    handlebars_string_immortalize(str);
}

static size_t calculate_size_operand(struct handlebars_module * module, struct handlebars_operand * operand)
{
    size_t i;
    size_t size = 0;

    // Increment for children
    switch( operand->type ) {
        case handlebars_operand_type_string:
            size += align_size(HBS_STR_SIZE(hbs_str_len(operand->data.string.string)));
            break;
        case handlebars_operand_type_array:
            size += align_size(sizeof(struct handlebars_operand_string) * operand->data.array.count);
            for( i = 0; i < operand->data.array.count; i++ ) {
                size += align_size(HBS_STR_SIZE(hbs_str_len(operand->data.array.array[i].string)));
            }
            break;
        default:
            // nothing
            break;
    }

    return size;
}

static size_t calculate_size_opcode(struct handlebars_module * module, struct handlebars_opcode * opcode)
{
    size_t size = 0;

    size += sizeof(struct handlebars_opcode);
    module->opcode_count++;

    size += calculate_size_operand(module, &opcode->op1);
    size += calculate_size_operand(module, &opcode->op2);
    size += calculate_size_operand(module, &opcode->op3);
    size += calculate_size_operand(module, &opcode->op4);

    return size;
}

static size_t calculate_size_program(struct handlebars_module * module, struct handlebars_program * program)
{
    size_t i;
    size_t size = 0;

    // Increment for self
    size += sizeof(struct handlebars_module_table_entry);
    module->program_count++;

    // Increment for children
    for( i = 0; i < program->children_length; i++ ) {
        size += calculate_size_program(module, program->children[i]);
    }

    // Increment for opcodes
    for( i = 0; i < program->opcodes_length; i++ ) {
        size += calculate_size_opcode(module, program->opcodes[i]);
    }

    // Insert return opcode
    struct handlebars_opcode opcode = {0};
    opcode.type = handlebars_opcode_type_return;
    size += calculate_size_opcode(module, &opcode);

    return size;
}

static void serialize_operand(struct handlebars_module * module, struct handlebars_operand * operand)
{
    size_t i;
    size_t size;

    // Increment for children
    switch( operand->type ) {
        case handlebars_operand_type_string:
            // Make sure hash is computed
            hbs_str_hash(operand->data.string.string);

            size = HBS_STR_SIZE(hbs_str_len(operand->data.string.string));
            operand->data.string.string = append(module, operand->data.string.string, size);
            patch_string(operand->data.string.string);
            break;
        case handlebars_operand_type_array:
            operand->data.array.array = append(module, operand->data.array.array, sizeof(struct handlebars_operand_string) * operand->data.array.count);
            for( i = 0; i < operand->data.array.count; i++ ) {
                // Make sure hash is computed
                hbs_str_hash(operand->data.array.array[i].string);

                size = HBS_STR_SIZE(hbs_str_len(operand->data.array.array[i].string));
                operand->data.array.array[i].string = append(module, operand->data.array.array[i].string, size);
                patch_string(operand->data.array.array[i].string);
            }
            break;
        default:
            // nothing
            break;
    }
}

static void serialize_opcode(struct handlebars_module * module, struct handlebars_opcode * opcode, struct handlebars_module_table_entry ** table)
{
    size_t guid = module->opcode_count++;
    struct handlebars_opcode * new_opcode = &module->opcodes[guid];

    // Copy
    *new_opcode = *opcode;

    // Serialize operands
    serialize_operand(module, &new_opcode->op1);
    serialize_operand(module, &new_opcode->op2);
    serialize_operand(module, &new_opcode->op3);
    serialize_operand(module, &new_opcode->op4);

    // Patch push_program opcode
    if( new_opcode->type == handlebars_opcode_type_push_program ) {
        if( new_opcode->op1.type == handlebars_operand_type_long && !new_opcode->op4.data.boolval ) {
            new_opcode->op1.data.longval = table[new_opcode->op1.data.longval]->guid;
            new_opcode->op4.data.boolval = 1;
        }
    }
}

static struct handlebars_module_table_entry * serialize_program_shallow(struct handlebars_module * module, struct handlebars_program * program)
{
    size_t guid = module->program_count++;
    struct handlebars_module_table_entry * entry = &module->programs[guid];

    entry->guid = guid;

    return entry;
}

static void serialize_program2(struct handlebars_module * module, struct handlebars_program * program, struct handlebars_module_table_entry * entry)
{
    size_t i;
    //struct handlebars_module_table_entry * children[program->children_length];
    struct handlebars_module_table_entry ** children = alloca(sizeof(struct handlebars_module_table_entry *) * program->children_length);

    // Serialize children (shallow)
    for( i = 0; i < program->children_length; i++ ) {
        children[i] = serialize_program_shallow(module, program->children[i]);
    }

    // Serialize opcodes
    entry->opcode_count = program->opcodes_length;
    entry->opcode_offset = module->opcode_count;
    for( i = 0 ; i < program->opcodes_length; i++ ) {
        serialize_opcode(module, program->opcodes[i], children);
    }

    // Insert return opcode
    struct handlebars_opcode opcode = {0};
    opcode.type = handlebars_opcode_type_return;
    serialize_opcode(module, &opcode, children);
    entry->opcode_count++;

    // Serialize children
    for( i = 0; i < program->children_length; i++ ) {
        serialize_program2(module, program->children[i], children[i]);
    }
}

static void serialize_program(struct handlebars_module * module, struct handlebars_program * program)
{
    struct handlebars_module_table_entry * entry = serialize_program_shallow(module, program);
    serialize_program2(module, program, entry);
}

struct handlebars_module * handlebars_program_serialize(
    struct handlebars_context * context,
    struct handlebars_program * program
) {
    // Allocate initial buffer
    struct handlebars_module * module = handlebars_talloc_zero(context, struct handlebars_module);
    HANDLEBARS_MEMCHECK(module, context);
    memcpy(&module->header, "HBSCM", sizeof("HBSCM"));
    module->version = handlebars_version();
    module->flags = program->flags;
    time(&module->ts);

    // Calculate size
    module->size = sizeof(struct handlebars_module) + calculate_size_program(module, program);

    // Reallocate buffer
    module = handlebars_talloc_realloc_size(context, module, module->size);
    HANDLEBARS_MEMCHECK(module, context);
    module->addr = (void *) module;
    talloc_set_type(module, struct handlebars_module);

    // Setup pointers
    size_t offset = 0;
    module->programs = (void *) &module->data[offset];
    offset += sizeof(struct handlebars_module_table_entry) * module->program_count;
    module->opcodes = (void *) &module->data[offset];
    offset += sizeof(struct handlebars_opcode) * module->opcode_count;

    // Reset counts - use as index
#ifndef NDEBUG
    size_t program_count = module->program_count;
    size_t opcode_count = module->opcode_count;
#endif

    module->program_count = module->opcode_count = 0;
    module->data_offset = offset;

    // Copy data
    serialize_program(module, program);

#ifndef NDEBUG
    assert(module->program_count == program_count);
    assert(module->opcode_count == opcode_count);
    assert(module->data_offset + sizeof(struct handlebars_module) == module->size);
#endif

    return module;
}




static inline void normalize_operand(struct handlebars_module * module, struct handlebars_operand * operand, void * baseaddr)
{
    size_t i;

    switch( operand->type ) {
        case handlebars_operand_type_string:
            PATCH(operand->data.string.string, baseaddr);
            break;
        case handlebars_operand_type_array:
            for( i = 0; i < operand->data.array.count; i++ ) {
                PATCH(operand->data.array.array[i].string, baseaddr);
            }
            PATCH(operand->data.array.array, baseaddr);
            break;
        default:
            // nothing
            break;
    }
}

void handlebars_module_normalize_pointers(struct handlebars_module * module, void *baseaddr)
{
    size_t i;

    if( module->addr == baseaddr ) {
        return;
    }

    for( i = 0; i < module->opcode_count; i++ ) {
        normalize_operand(module, &module->opcodes[i].op1, baseaddr);
        normalize_operand(module, &module->opcodes[i].op2, baseaddr);
        normalize_operand(module, &module->opcodes[i].op3, baseaddr);
        normalize_operand(module, &module->opcodes[i].op4, baseaddr);
    }

    PATCH(module->programs, baseaddr);
    PATCH(module->opcodes, baseaddr);

    module->addr = baseaddr;
}

static inline void patch_operand(struct handlebars_module * module, struct handlebars_operand * operand, void * baseaddr)
{
    size_t i;

    switch( operand->type ) {
        case handlebars_operand_type_string:
            PATCH(operand->data.string.string, baseaddr);
            break;
        case handlebars_operand_type_array:
            PATCH(operand->data.array.array, baseaddr);
            for( i = 0; i < operand->data.array.count; i++ ) {
                PATCH(operand->data.array.array[i].string, baseaddr);
            }
            break;
        default:
            // nothing
            break;
    }
}

void handlebars_module_patch_pointers(struct handlebars_module * module)
{
    size_t i;
    void *baseaddr = (void *) module;

    if( module->addr == baseaddr ) {
        return;
    }

    PATCH(module->programs, baseaddr);
    PATCH(module->opcodes, baseaddr);

    for( i = 0; i < module->opcode_count; i++ ) {
        patch_operand(module, &module->opcodes[i].op1, baseaddr);
        patch_operand(module, &module->opcodes[i].op2, baseaddr);
        patch_operand(module, &module->opcodes[i].op3, baseaddr);
        patch_operand(module, &module->opcodes[i].op4, baseaddr);
    }

    module->addr = baseaddr;
}

size_t handlebars_module_get_size(struct handlebars_module * module)
{
    return module->size;
}

int handlebars_module_get_version(struct handlebars_module * module)
{
    return module->version;
}

time_t handlebars_module_get_ts(struct handlebars_module * module)
{
    return module->ts;
}

long handlebars_module_get_flags(struct handlebars_module * module)
{
    return module->flags;
}

uint64_t handlebars_module_get_hash(struct handlebars_module * module)
{
    return module->hash;
}

static uint64_t calculate_hash(struct handlebars_module * module)
{
    void * start = &module->version;
    size_t size = module->size - offsetof(struct handlebars_module, version);
    return handlebars_hash_xxh3((const char *) start, size);
}

uint64_t handlebars_module_generate_hash(
    struct handlebars_module * module
) {
    return module->hash = calculate_hash(module);
}

bool handlebars_module_verify(
    struct handlebars_module * module,
    struct handlebars_context * ctx
) {
    uint64_t hash = calculate_hash(module);
    bool matched = true;
    if (hash != module->hash) {
        if (ctx != NULL) {
            handlebars_throw(
                ctx,
                HANDLEBARS_ERROR,
                "Invalid module hash expected=%llu actual=%llu",
                (unsigned long long) module->hash,
                (unsigned long long) hash
            );
        }
        matched = false;
    }
    if (handlebars_version() != module->version) {
        if (ctx != NULL) {
            handlebars_throw(
                ctx,
                HANDLEBARS_ERROR,
                "Invalid module version expected=%llu actual=%llu",
                (unsigned long long) module->version,
                (unsigned long long) handlebars_version()
            );
        }
        matched = false;
    }
    return matched;
}

// Checks that `ptr` — a not-yet-patched pointer stored relative to
// module->addr — resolves to a byte offset within [min_offset, size] that has
// room for `span` more bytes. All arithmetic is overflow-safe. On success the
// resolved offset is written to *out_offset.
static bool module_offset_ok(
    const struct handlebars_module * module,
    const void * ptr,
    size_t min_offset,
    size_t span,
    size_t * out_offset
) {
    uintptr_t base = (uintptr_t) module->addr;
    uintptr_t p = (uintptr_t) ptr;
    size_t offset;

    if (p < base) {
        return false;
    }
    offset = (size_t) (p - base);
    if (offset < min_offset || offset > module->size) {
        return false;
    }
    if (span > module->size - offset) {
        return false;
    }
    if (out_offset != NULL) {
        *out_offset = offset;
    }
    return true;
}

// Validates that an operand string pointer lands within the data region with
// room for the whole string (header + contents).
static bool module_validate_string(
    const struct handlebars_module * module,
    struct handlebars_string * str,
    size_t data_start
) {
    size_t offset;
    struct handlebars_string * real;
    size_t len;
    size_t body_room;

    // First ensure there is room for the string header so reading its length is safe
    if (!module_offset_ok(module, str, data_start, HANDLEBARS_STRING_SIZE, &offset)) {
        return false;
    }
    real = (struct handlebars_string *) ((char *) module + offset);
    len = hbs_str_len(real);
    // Then ensure the declared contents fit as well. module_offset_ok already
    // guaranteed HANDLEBARS_STRING_SIZE bytes of header, so body_room cannot
    // underflow; comparing len against it (rather than computing HBS_STR_SIZE(len),
    // which would overflow for an attacker-supplied len near SIZE_MAX) is
    // overflow-safe. Need HANDLEBARS_STRING_SIZE + len + 1 <= module->size - offset,
    // i.e. len + 1 <= body_room, i.e. len < body_room.
    body_room = (module->size - offset) - HANDLEBARS_STRING_SIZE;
    if (len >= body_room) {
        return false;
    }
    return true;
}

static bool module_validate_operand(
    const struct handlebars_module * module,
    const struct handlebars_operand * operand,
    size_t data_start
) {
    size_t i;
    size_t offset;
    size_t count;
    struct handlebars_operand_string * arr;

    switch (operand->type) {
        case handlebars_operand_type_string:
            return module_validate_string(module, operand->data.string.string, data_start);
        case handlebars_operand_type_array:
            count = operand->data.array.count;
            // Overflow-safe bound on the array-of-strings block
            if (count > (module->size - data_start) / sizeof(struct handlebars_operand_string)) {
                return false;
            }
            if (!module_offset_ok(module, operand->data.array.array, data_start,
                    count * sizeof(struct handlebars_operand_string), &offset)) {
                return false;
            }
            arr = (struct handlebars_operand_string *) ((char *) module + offset);
            for (i = 0; i < count; i++) {
                if (!module_validate_string(module, arr[i].string, data_start)) {
                    return false;
                }
            }
            return true;
        default:
            return true;
    }
}

bool handlebars_module_validate(
    struct handlebars_module * module,
    size_t actual_size,
    struct handlebars_context * ctx
) {
    const size_t header_size = sizeof(struct handlebars_module);
    const size_t entry_size = sizeof(struct handlebars_module_table_entry);
    const size_t opcode_size = sizeof(struct handlebars_opcode);
    size_t data_size;
    size_t programs_bytes;
    size_t opcodes_bytes;
    size_t opcodes_end;
    size_t off;
    size_t i;
    struct handlebars_module_table_entry * programs;
    struct handlebars_opcode * opcodes;

#define FAIL(...) do { \
        if (ctx != NULL) { handlebars_throw(ctx, HANDLEBARS_ERROR, __VA_ARGS__); } \
        return false; \
    } while (0)

    // The fixed header (through data_offset) must be present before we trust any
    // size field inside it.
    if (actual_size < header_size) {
        FAIL("Invalid module: buffer smaller than header (%zu < %zu)", actual_size, header_size);
    }
    if (memcmp(module->header, "HBSCM", sizeof("HBSCM")) != 0) {
        FAIL("Invalid module: bad magic");
    }
    if (module->version != handlebars_version()) {
        FAIL("Invalid module version expected=%d actual=%d", module->version, handlebars_version());
    }
    // The declared size must be at least the header and must fit within the
    // backing buffer. It may be smaller than actual_size for a backend that
    // embeds the module in a larger region (mmap); all interior bounds below are
    // checked against module->size, so reads stay within the buffer either way.
    if (module->size < header_size) {
        FAIL("Invalid module: declared size %zu smaller than header %zu", module->size, header_size);
    }
    if (module->size > actual_size) {
        FAIL("Invalid module: declared size %zu exceeds buffer size %zu", module->size, actual_size);
    }

    data_size = module->size - header_size;

    // Counts must be small enough that their arrays fit contiguously; the
    // divisions keep the multiplications from overflowing.
    if (module->program_count > data_size / entry_size) {
        FAIL("Invalid module: program_count %zu out of range", module->program_count);
    }
    programs_bytes = module->program_count * entry_size;
    if (module->opcode_count > (data_size - programs_bytes) / opcode_size) {
        FAIL("Invalid module: opcode_count %zu out of range", module->opcode_count);
    }
    opcodes_bytes = module->opcode_count * opcode_size;
    opcodes_end = header_size + programs_bytes + opcodes_bytes;

    // The two arrays must sit at exactly the offsets the serializer writes.
    if (!module_offset_ok(module, module->programs, header_size, programs_bytes, &off) || off != header_size) {
        FAIL("Invalid module: programs array out of range");
    }
    if (!module_offset_ok(module, module->opcodes, header_size + programs_bytes, opcodes_bytes, &off)
            || off != header_size + programs_bytes) {
        FAIL("Invalid module: opcodes array out of range");
    }

    programs = (struct handlebars_module_table_entry *) ((char *) module + header_size);
    opcodes = (struct handlebars_opcode *) ((char *) module + header_size + programs_bytes);

    // Each program's opcode window must lie within the opcode array.
    for (i = 0; i < module->program_count; i++) {
        if (programs[i].opcode_offset > module->opcode_count) {
            FAIL("Invalid module: program %zu opcode_offset out of range", i);
        }
        if (programs[i].opcode_count > module->opcode_count - programs[i].opcode_offset) {
            FAIL("Invalid module: program %zu opcode_count out of range", i);
        }
        // The VM dispatch loop starts at opcode_offset and steps opcode-by-opcode
        // until it hits a return. A program that is empty or does not end with a
        // return would therefore read opcodes past its window - and ultimately
        // past the opcode array - out of bounds. The serializer always emits a
        // trailing return, so require one here.
        if (programs[i].opcode_count < 1) {
            FAIL("Invalid module: program %zu has no opcodes", i);
        }
        if (opcodes[programs[i].opcode_offset + programs[i].opcode_count - 1].type
                != handlebars_opcode_type_return) {
            FAIL("Invalid module: program %zu does not end with a return opcode", i);
        }
    }

    // Every string/array operand must point inside the data region.
    for (i = 0; i < module->opcode_count; i++) {
        if (!module_validate_operand(module, &opcodes[i].op1, opcodes_end) ||
            !module_validate_operand(module, &opcodes[i].op2, opcodes_end) ||
            !module_validate_operand(module, &opcodes[i].op3, opcodes_end) ||
            !module_validate_operand(module, &opcodes[i].op4, opcodes_end)) {
            FAIL("Invalid module: opcode %zu operand out of range", i);
        }
    }

#undef FAIL
    return true;
}
