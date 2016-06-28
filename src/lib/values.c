/* vi:set ts=4 sw=4 expandtab:
 *
 * Copyright 2016, Chris Leishman (http://github.com/cleishm)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "../../config.h"
#include "values.h"
#include "iostream.h"
#include "print.h"
#include "serialization.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>


static bool null_eq(const neo4j_value_t *value, const neo4j_value_t *other);
static bool bool_eq(const neo4j_value_t *value, const neo4j_value_t *other);
static bool int_eq(const neo4j_value_t *value, const neo4j_value_t *other);
static bool float_eq(const neo4j_value_t *value, const neo4j_value_t *other);
static bool string_eq(const neo4j_value_t *value, const neo4j_value_t *other);
static bool list_eq(const neo4j_value_t *value, const neo4j_value_t *other);
static bool map_eq(const neo4j_value_t *value, const neo4j_value_t *other);
static bool struct_eq(const neo4j_value_t *value, const neo4j_value_t *other);


/* types */

struct neo4j_type
{
    const char *name;
};

static const struct neo4j_type null_type = { .name = "Null" };
static const struct neo4j_type bool_type = { .name = "Boolean" };
static const struct neo4j_type int_type = { .name = "Integer" };
static const struct neo4j_type float_type = { .name = "Float" };
static const struct neo4j_type string_type = { .name = "String" };
static const struct neo4j_type list_type = { .name = "List" };
static const struct neo4j_type map_type = { .name = "Map" };
static const struct neo4j_type node_type = { .name = "Node" };
static const struct neo4j_type relationship_type = { .name = "Relationship" };
static const struct neo4j_type path_type = { .name = "Path" };
static const struct neo4j_type identity_type = { .name = "Identity" };
static const struct neo4j_type struct_type = { .name = "Struct" };

static const struct neo4j_type *neo4j_types[] =
    { &null_type,
      &bool_type,
      &int_type,
      &float_type,
      &string_type,
      &list_type,
      &map_type,
      &node_type,
      &relationship_type,
      &path_type,
      &identity_type,
      &struct_type };

#define NULL_TYPE_OFF 0
const uint8_t NEO4J_NULL = NULL_TYPE_OFF;
#define BOOL_TYPE_OFF 1
const uint8_t NEO4J_BOOL = BOOL_TYPE_OFF;
#define INT_TYPE_OFF 2
const uint8_t NEO4J_INT = INT_TYPE_OFF;
#define FLOAT_TYPE_OFF 3
const uint8_t NEO4J_FLOAT = FLOAT_TYPE_OFF;
#define STRING_TYPE_OFF 4
const uint8_t NEO4J_STRING = STRING_TYPE_OFF;
#define LIST_TYPE_OFF 5
const uint8_t NEO4J_LIST = LIST_TYPE_OFF;
#define MAP_TYPE_OFF 6
const uint8_t NEO4J_MAP = MAP_TYPE_OFF;
#define NODE_TYPE_OFF 7
const uint8_t NEO4J_NODE = NODE_TYPE_OFF;
#define RELATIONSHIP_TYPE_OFF 8
const uint8_t NEO4J_RELATIONSHIP = RELATIONSHIP_TYPE_OFF;
#define PATH_TYPE_OFF 9
const uint8_t NEO4J_PATH = PATH_TYPE_OFF;
#define IDENTITY_TYPE_OFF 10
const uint8_t NEO4J_IDENTITY = IDENTITY_TYPE_OFF;
#define STRUCT_TYPE_OFF 11
const uint8_t NEO4J_STRUCT = STRUCT_TYPE_OFF;
static const uint8_t _MAX_TYPE =
    (sizeof(neo4j_types) / sizeof(struct neo4j_type *));

static_assert(
    (sizeof(neo4j_types) / sizeof(struct neo4j_type *)) <= UINT8_MAX,
    "value vt table cannot hold more than 2^8 entries");


bool neo4j_instanceof(neo4j_value_t value, neo4j_type_t type)
{
    // currently, values do not have any inheritance
    return neo4j_type(value) == type;
}


const char *neo4j_typestr(const neo4j_type_t type)
{
    assert(type < _MAX_TYPE);
    return neo4j_types[type]->name;
}


/* vector tables */

struct neo4j_value_vt
{
    size_t (*str)(const neo4j_value_t *self, char *buf, size_t n);
    ssize_t (*wstr)(const neo4j_value_t *self, wchar_t *buf, size_t n);
    ssize_t (*fprint)(const neo4j_value_t *self, FILE *stream);
    int (*serialize)(const neo4j_value_t *self, neo4j_iostream_t *stream);
    bool (*eq)(const neo4j_value_t *self, const neo4j_value_t *other);
};

static struct neo4j_value_vt null_vt =
    { .str = neo4j_null_str,
      .wstr = neo4j_null_wstr,
      .fprint = neo4j_null_fprint,
      .serialize = neo4j_null_serialize,
      .eq = null_eq };
static struct neo4j_value_vt bool_vt =
    { .str = neo4j_bool_str,
      .wstr = neo4j_bool_wstr,
      .fprint = neo4j_bool_fprint,
      .serialize = neo4j_bool_serialize,
      .eq = bool_eq };
static struct neo4j_value_vt int_vt =
    { .str = neo4j_int_str,
      .wstr = neo4j_int_wstr,
      .fprint = neo4j_int_fprint,
      .serialize = neo4j_int_serialize,
      .eq = int_eq };
static struct neo4j_value_vt float_vt =
    { .str = neo4j_float_str,
      .wstr = neo4j_float_wstr,
      .fprint = neo4j_float_fprint,
      .serialize = neo4j_float_serialize,
      .eq = float_eq };
static struct neo4j_value_vt string_vt =
    { .str = neo4j_string_str,
      .fprint = neo4j_string_fprint,
      .serialize = neo4j_string_serialize,
      .eq = string_eq };
static struct neo4j_value_vt list_vt =
    { .str = neo4j_list_str,
      .fprint = neo4j_list_fprint,
      .serialize = neo4j_list_serialize,
      .eq = list_eq };
static struct neo4j_value_vt map_vt =
    { .str = neo4j_map_str,
      .fprint = neo4j_map_fprint,
      .serialize = neo4j_map_serialize,
      .eq = map_eq };
static struct neo4j_value_vt node_vt =
    { .str = neo4j_node_str,
      .fprint = neo4j_node_fprint,
      .serialize = neo4j_struct_serialize,
      .eq = struct_eq };
static struct neo4j_value_vt relationship_vt =
    { .str = neo4j_rel_str,
      .fprint = neo4j_rel_fprint,
      .serialize = neo4j_struct_serialize,
      .eq = struct_eq };
static struct neo4j_value_vt path_vt =
    { .str = neo4j_path_str,
      .fprint = neo4j_path_fprint,
      .serialize = neo4j_struct_serialize,
      .eq = struct_eq };
static struct neo4j_value_vt identity_vt =
    { .str = neo4j_int_str,
      .fprint = neo4j_int_fprint,
      .serialize = neo4j_int_serialize,
      .eq = int_eq };
static struct neo4j_value_vt struct_vt =
    { .str = neo4j_struct_str,
      .fprint = neo4j_struct_fprint,
      .serialize = neo4j_struct_serialize,
      .eq = struct_eq };

static const struct neo4j_value_vt *neo4j_value_vts[] =
    { &null_vt,
      &bool_vt,
      &int_vt,
      &float_vt,
      &string_vt,
      &list_vt,
      &map_vt,
      &node_vt,
      &relationship_vt,
      &path_vt,
      &identity_vt,
      &struct_vt };

#define NULL_VT_OFF 0
#define BOOL_VT_OFF 1
#define INT_VT_OFF 2
#define FLOAT_VT_OFF 3
#define STRING_VT_OFF 4
#define LIST_VT_OFF 5
#define MAP_VT_OFF 6
#define NODE_VT_OFF 7
#define RELATIONSHIP_VT_OFF 8
#define PATH_VT_OFF 9
#define IDENTITY_VT_OFF 10
#define STRUCT_VT_OFF 11
#define _MAX_VT_OFF (sizeof(neo4j_value_vts) / sizeof(struct neo4j_value_vt *))

static_assert(
    (sizeof(neo4j_value_vts) / sizeof(struct neo4j_value_vt *)) <= UINT8_MAX,
    "value vt table cannot hold more than 2^8 entries");


/* method dispatch */

char *neo4j_tostring(neo4j_value_t value, char *buf, size_t n)
{
    neo4j_ntostring(value, buf, n);
    return buf;
}


size_t neo4j_ntostring(neo4j_value_t value, char *buf, size_t n)
{
    REQUIRE(value._vt_off < _MAX_VT_OFF, -1);
    REQUIRE(value._type < _MAX_TYPE, -1);
    const struct neo4j_value_vt *vt = neo4j_value_vts[value._vt_off];
    return vt->str(&value, buf, n);
}


ssize_t neo4j_ntowstring(neo4j_value_t value, wchar_t *wbuf, size_t n)
{
    REQUIRE(value._vt_off < _MAX_VT_OFF, -1);
    REQUIRE(value._type < _MAX_TYPE, -1);
    const struct neo4j_value_vt *vt = neo4j_value_vts[value._vt_off];
    return vt->wstr(&value, wbuf, n);
}


ssize_t neo4j_fprint(neo4j_value_t value, FILE *stream)
{
    REQUIRE(value._vt_off < _MAX_VT_OFF, -1);
    REQUIRE(value._type < _MAX_TYPE, -1);
    const struct neo4j_value_vt *vt = neo4j_value_vts[value._vt_off];
    return vt->fprint(&value, stream);
}


int neo4j_serialize(neo4j_value_t value, neo4j_iostream_t *stream)
{
    REQUIRE(value._vt_off < _MAX_VT_OFF, -1);
    REQUIRE(value._type < _MAX_TYPE, -1);
    const struct neo4j_value_vt *vt = neo4j_value_vts[value._vt_off];
    return vt->serialize(&value, stream);
}


bool neo4j_eq(neo4j_value_t value1, neo4j_value_t value2)
{
    REQUIRE(value1._vt_off < _MAX_VT_OFF, false);
    REQUIRE(value1._type < _MAX_TYPE, false);
    errno = 0;
    REQUIRE(neo4j_type(value1) == neo4j_type(value2), false);
    const struct neo4j_value_vt *vt = neo4j_value_vts[value1._vt_off];
    return vt->eq(&value1, &value2);
}


/* constructors and accessors */

// null

const neo4j_value_t neo4j_null =
    { ._type = NULL_TYPE_OFF, ._vt_off = NULL_VT_OFF };


static bool null_eq(const neo4j_value_t *value, const neo4j_value_t *other)
{
    return true;
}

// bool

neo4j_value_t neo4j_bool(bool value)
{
    struct neo4j_bool v =
        { ._type = NEO4J_BOOL, ._vt_off = BOOL_VT_OFF,
          .value = value? 1 : 0 };
    return *((neo4j_value_t *)(&v));
}


bool bool_eq(const neo4j_value_t *value, const neo4j_value_t *other)
{
    const struct neo4j_bool *v = (const struct neo4j_bool *)value;
    const struct neo4j_bool *o = (const struct neo4j_bool *)other;
    return v->value == o->value;
}


bool neo4j_bool_value(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_BOOL, false);
    return ((const struct neo4j_bool *)&value)->value? true : false;
}


// int

neo4j_value_t neo4j_int(long long value)
{
#if LLONG_MIN != INT64_MIN || LLONG_MAX != INT64_MAX
    if (value < INT64_MIN)
    {
        value = INT64_MIN;
    }
    else if (value > INT64_MAX)
    {
        value = INT64_MAX;
    }
#endif
    struct neo4j_int v =
        { ._type = NEO4J_INT, ._vt_off = INT_VT_OFF, .value = value };
    return *((neo4j_value_t *)(&v));
}


bool int_eq(const neo4j_value_t *value, const neo4j_value_t *other)
{
    const struct neo4j_int *v = (const struct neo4j_int *)value;
    const struct neo4j_int *o = (const struct neo4j_int *)other;
    return v->value == o->value;
}


long long neo4j_int_value(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_INT, 0);
    return ((const struct neo4j_int *)&value)->value;
}


// float

neo4j_value_t neo4j_float(double value)
{
    struct neo4j_float v =
        { ._type = NEO4J_FLOAT, ._vt_off = FLOAT_VT_OFF, .value = value };
    return *((neo4j_value_t *)(&v));
}


bool float_eq(const neo4j_value_t *value, const neo4j_value_t *other)
{
    const struct neo4j_float *v = (const struct neo4j_float *)value;
    const struct neo4j_float *o = (const struct neo4j_float *)other;
    return v->value == o->value;
}


double neo4j_float_value(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_FLOAT, 0.0);
    return ((const struct neo4j_float *)&value)->value;
}


// string

neo4j_value_t neo4j_ustring(const char *u, unsigned int n)
{
#if UINT_MAX != UINT32_MAX
    if (n > UINT32_MAX)
    {
        n = UINT32_MAX;
    }
#endif
    struct neo4j_string v =
        { ._type = NEO4J_STRING, ._vt_off = STRING_VT_OFF,
          .ustring = u, .length = n };
    return *((neo4j_value_t *)(&v));
}


bool string_eq(const neo4j_value_t *value, const neo4j_value_t *other)
{
    const struct neo4j_string *v = (const struct neo4j_string *)value;
    const struct neo4j_string *o = (const struct neo4j_string *)other;
    if (v->length != o->length)
    {
        return false;
    }
    return strncmp(v->ustring, o->ustring, v->length) == 0;
}


unsigned int neo4j_string_length(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_STRING, 0);
    return ((const struct neo4j_string *)&value)->length;
}


const char *neo4j_ustring_value(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_STRING, NULL);
    return ((const struct neo4j_string *)&value)->ustring;
}


char *neo4j_string_value(neo4j_value_t value, char *buffer, size_t length)
{
    REQUIRE(neo4j_type(value) == NEO4J_STRING, NULL);
    const struct neo4j_string *v = (const struct neo4j_string *)&value;
    size_t tocopy = min(v->length, length-1);
    memcpy(buffer, v->ustring, tocopy);
    buffer[tocopy] = '\0';
    return buffer;
}


ssize_t neo4j_string_wvalue(neo4j_value_t value, wchar_t *wbuffer,
        size_t length)
{
    REQUIRE(neo4j_type(value) == NEO4J_STRING, -1);
    mbstate_t ps;
    memset(&ps, 0, sizeof(ps));
    const struct neo4j_string *v = (const struct neo4j_string *)&value;
    const char *s = v->ustring;
    size_t n = mbsnrtowcs(wbuffer, &s, v->length, length, &ps);
    if (n == (size_t)-1)
    {
        return -1;
    }
    return (ssize_t)n;
}


// list

neo4j_value_t neo4j_list(const neo4j_value_t *items, unsigned int n)
{
#if UINT_MAX != UINT32_MAX
    if (n > UINT32_MAX)
    {
        n = UINT32_MAX;
    }
#endif
    struct neo4j_list v =
        { ._type = NEO4J_LIST, ._vt_off = LIST_VT_OFF,
          .items = items, .length = n };
    return *((neo4j_value_t *)(&v));
}


bool list_eq(const neo4j_value_t *value, const neo4j_value_t *other)
{
    const struct neo4j_list *v = (const struct neo4j_list *)value;
    const struct neo4j_list *o = (const struct neo4j_list *)other;

    if (v->length != o->length)
    {
        return false;
    }

    for (unsigned int i = 0; i < v->length; ++i)
    {
        if (!neo4j_eq(v->items[i], o->items[i]))
        {
            return false;
        }
    }

    return true;
}


unsigned int neo4j_list_length(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_LIST, 0);
    return ((const struct neo4j_list *)&value)->length;
}


neo4j_value_t neo4j_list_get(neo4j_value_t value, unsigned int index)
{
    REQUIRE(neo4j_type(value) == NEO4J_LIST, neo4j_null);
    const struct neo4j_list *list = (const struct neo4j_list *)&value;
    if (list->length <= index)
    {
        return neo4j_null;
    }
    return list->items[index];
}


// map

neo4j_value_t neo4j_map(const neo4j_map_entry_t *entries, unsigned int n)
{
#if UINT_MAX != UINT32_MAX
    if (n > UINT32_MAX)
    {
        n = UINT32_MAX;
    }
#endif
    for (unsigned int i = 0; i < n; ++i)
    {
        if (neo4j_type(entries[i].key) != NEO4J_STRING)
        {
            errno = NEO4J_INVALID_MAP_KEY_TYPE;
            return neo4j_null;
        }
    }

    struct neo4j_map v =
        { ._type = NEO4J_MAP, ._vt_off = MAP_VT_OFF,
          .entries = entries, .nentries = n };
    return *((neo4j_value_t *)(&v));
}


bool map_eq(const neo4j_value_t *value, const neo4j_value_t *other)
{
    const struct neo4j_map *v = (const struct neo4j_map *)value;
    const struct neo4j_map *o = (const struct neo4j_map *)other;

    if (v->nentries != o->nentries)
    {
        return false;
    }

    for (unsigned int i = 0; i < v->nentries; ++i)
    {
        const neo4j_map_entry_t *ventry = &(v->entries[i]);
        const neo4j_map_entry_t *oentry = NULL;
        for (unsigned int j = 0; j < o->nentries; ++j)
        {
            if (neo4j_eq(ventry->key, o->entries[j].key))
            {
                oentry = &(o->entries[j]);
                break;
            }
        }
        if (oentry == NULL || !neo4j_eq(ventry->value, oentry->value))
        {
            return false;
        }
    }
    return true;
}


unsigned int neo4j_map_size(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_MAP, 0);
    return ((const struct neo4j_map *)&value)->nentries;
}


const neo4j_map_entry_t *neo4j_map_getentry(neo4j_value_t value,
        unsigned int index)
{
    REQUIRE(neo4j_type(value) == NEO4J_MAP, 0);
    const struct neo4j_map *map = (const struct neo4j_map *)&value;

    if (map->nentries <= index)
    {
        return NULL;
    }
    return &(map->entries[index]);
}


neo4j_value_t neo4j_map_kget(neo4j_value_t value, neo4j_value_t key)
{
    REQUIRE(neo4j_type(value) == NEO4J_MAP, neo4j_null);
    const struct neo4j_map *map = (const struct neo4j_map *)&value;

    for (unsigned int i = 0; i < map->nentries; ++i)
    {
        const neo4j_map_entry_t *entry = &(map->entries[i]);
        if (neo4j_eq(entry->key, key))
        {
            return entry->value;
        }
    }
    return neo4j_null;
}


neo4j_map_entry_t neo4j_map_kentry(neo4j_value_t key, neo4j_value_t value)
{
    struct neo4j_map_entry entry = { .key = key, .value = value };
    return entry;
}


// node

neo4j_value_t neo4j_node(const neo4j_value_t fields[3])
{
    if (neo4j_type(fields[0]) != NEO4J_IDENTITY ||
            neo4j_type(fields[1]) != NEO4J_LIST ||
            neo4j_type(fields[2]) != NEO4J_MAP)
    {
        errno = EINVAL;
        return neo4j_null;
    }
    const struct neo4j_list *labels = (const struct neo4j_list *)&(fields[1]);
    for (unsigned int i = 0; i < labels->length; ++i)
    {
        if (neo4j_type(labels->items[i]) != NEO4J_STRING)
        {
            errno = NEO4J_INVALID_LABEL_TYPE;
            return neo4j_null;
        }
    }

    struct neo4j_struct v =
            { ._type = NEO4J_NODE, ._vt_off = NODE_VT_OFF,
              .signature = NEO4J_NODE_SIGNATURE,
              .fields = fields, .nfields = 3 };
    return *((neo4j_value_t *)(&v));
}


neo4j_value_t neo4j_node_labels(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_NODE, neo4j_null);
    const struct neo4j_struct *v = (const struct neo4j_struct *)&value;
    assert(v->nfields == 3);
    assert(neo4j_type(v->fields[1]) == NEO4J_LIST);
    return v->fields[1];
}


neo4j_value_t neo4j_node_properties(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_NODE, neo4j_null);
    const struct neo4j_struct *v = (const struct neo4j_struct *)&value;
    assert(v->nfields == 3);
    assert(neo4j_type(v->fields[2]) == NEO4J_MAP);
    return v->fields[2];
}


neo4j_value_t neo4j_node_identity(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_NODE, neo4j_null);
    const struct neo4j_struct *v = (const struct neo4j_struct *)&value;
    assert(v->nfields == 3);
    assert(neo4j_type(v->fields[0]) == NEO4J_IDENTITY);
    return v->fields[0];
}


// relationship

neo4j_value_t neo4j_relationship(const neo4j_value_t fields[5])
{
    if (neo4j_type(fields[0]) != NEO4J_IDENTITY ||
            (neo4j_type(fields[1]) != NEO4J_IDENTITY &&
                !neo4j_is_null(fields[1])) ||
            (neo4j_type(fields[2]) != NEO4J_IDENTITY &&
                !neo4j_is_null(fields[1])) ||
            neo4j_type(fields[3]) != NEO4J_STRING ||
            neo4j_type(fields[4]) != NEO4J_MAP)
    {
        errno = EINVAL;
        return neo4j_null;
    }

    struct neo4j_struct v =
            { ._type = NEO4J_RELATIONSHIP, ._vt_off = RELATIONSHIP_VT_OFF,
              .signature = NEO4J_REL_SIGNATURE,
              .fields = fields, .nfields = 5 };
    return *((neo4j_value_t *)(&v));
}


neo4j_value_t neo4j_unbound_relationship(const neo4j_value_t fields[3])
{
    if (neo4j_type(fields[0]) != NEO4J_IDENTITY ||
            neo4j_type(fields[1]) != NEO4J_STRING ||
            neo4j_type(fields[2]) != NEO4J_MAP)
    {
        errno = EINVAL;
        return neo4j_null;
    }

    struct neo4j_struct v =
            { ._type = NEO4J_RELATIONSHIP, ._vt_off = RELATIONSHIP_VT_OFF,
              .signature = NEO4J_REL_SIGNATURE,
              .fields = fields, .nfields = 3 };
    return *((neo4j_value_t *)(&v));
}


neo4j_value_t neo4j_relationship_type(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_RELATIONSHIP, neo4j_null);
    const struct neo4j_struct *v = (const struct neo4j_struct *)&value;
    if (v->nfields == 5)
    {
        assert(neo4j_type(v->fields[3]) == NEO4J_STRING);
        return v->fields[3];
    }
    else
    {
        assert(v->nfields == 3);
        assert(neo4j_type(v->fields[1]) == NEO4J_STRING);
        return v->fields[1];
    }
}


neo4j_value_t neo4j_relationship_properties(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_RELATIONSHIP, neo4j_null);
    const struct neo4j_struct *v = (const struct neo4j_struct *)&value;
    if (v->nfields == 5)
    {
        assert(neo4j_type(v->fields[4]) == NEO4J_MAP);
        return v->fields[4];
    }
    else
    {
        assert(v->nfields == 3);
        assert(neo4j_type(v->fields[2]) == NEO4J_MAP);
        return v->fields[2];
    }
}


neo4j_value_t neo4j_relationship_identity(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_RELATIONSHIP, neo4j_null);
    const struct neo4j_struct *v = (const struct neo4j_struct *)&value;
    assert(v->nfields == 3 || v->nfields == 5);
    assert(neo4j_type(v->fields[0]) == NEO4J_IDENTITY);
    return v->fields[0];
}


neo4j_value_t neo4j_relationship_start_node_identity(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_RELATIONSHIP, neo4j_null);
    const struct neo4j_struct *v = (const struct neo4j_struct *)&value;
    if (v->nfields == 5)
    {
        assert(neo4j_type(v->fields[1]) == NEO4J_IDENTITY);
        return v->fields[1];
    }
    else
    {
        return neo4j_null;
    }
}


neo4j_value_t neo4j_relationship_end_node_identity(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_RELATIONSHIP, neo4j_null);
    const struct neo4j_struct *v = (const struct neo4j_struct *)&value;
    if (v->nfields == 5)
    {
        assert(neo4j_type(v->fields[2]) == NEO4J_IDENTITY);
        return v->fields[2];
    }
    else
    {
        return neo4j_null;
    }
}


// path

neo4j_value_t neo4j_path(const neo4j_value_t fields[3])
{
    if (neo4j_type(fields[0]) != NEO4J_LIST ||
            neo4j_type(fields[1]) != NEO4J_LIST ||
            neo4j_type(fields[2]) != NEO4J_LIST)
    {
        errno = EINVAL;
        return neo4j_null;
    }

    const struct neo4j_list *nodes = (const struct neo4j_list *)&(fields[0]);
    for (unsigned int i = 0; i < nodes->length; ++i)
    {
        if (neo4j_type(nodes->items[i]) != NEO4J_NODE)
        {
            errno = NEO4J_INVALID_PATH_NODE_TYPE;
            return neo4j_null;
        }
    }

    const struct neo4j_list *rels = (const struct neo4j_list *)&(fields[1]);
    for (unsigned int i = 0; i < rels->length; ++i)
    {
        if (neo4j_type(rels->items[i]) != NEO4J_RELATIONSHIP)
        {
            errno = NEO4J_INVALID_PATH_RELATIONSHIP_TYPE;
            return neo4j_null;
        }
    }

    const struct neo4j_list *seq = (const struct neo4j_list *)&(fields[2]);
    if ((seq->length % 2) != 0)
    {
        errno = NEO4J_INVALID_PATH_SEQUENCE_LENGTH;
        return neo4j_null;
    }
    for (unsigned int i = 0; i < seq->length; i += 2)
    {
        if (neo4j_type(seq->items[i]) != NEO4J_INT ||
            neo4j_type(seq->items[i+1]) != NEO4J_INT)
        {
            errno = NEO4J_INVALID_PATH_SEQUENCE_IDX_TYPE;
            return neo4j_null;
        }
        const struct neo4j_int *idx =
            (const struct neo4j_int *)&(seq->items[i]);
        if (idx->value == 0 || idx->value > rels->length ||
            -(idx->value) > rels->length)
        {
            errno = NEO4J_INVALID_PATH_SEQUENCE_IDX_RANGE;
            return neo4j_null;
        }

        idx = (const struct neo4j_int *)&(seq->items[i+1]);
        if (idx->value < 0 || idx->value >= nodes->length)
        {
            errno = NEO4J_INVALID_PATH_SEQUENCE_IDX_RANGE;
            return neo4j_null;
        }
    }

    struct neo4j_struct v =
            { ._type = NEO4J_PATH, ._vt_off = PATH_VT_OFF,
              .signature = NEO4J_PATH_SIGNATURE,
              .fields = fields, .nfields = 3 };
    return *((neo4j_value_t *)(&v));
}


unsigned int neo4j_path_length(neo4j_value_t value)
{
    REQUIRE(neo4j_type(value) == NEO4J_PATH, 0);
    const struct neo4j_struct *v = (const struct neo4j_struct *)&value;
    assert(v->nfields == 3);
    assert(neo4j_type(v->fields[2]) == NEO4J_LIST);
    unsigned int slength = neo4j_list_length(v->fields[2]);
    assert((slength % 2) == 0);
    return slength / 2;
}


neo4j_value_t neo4j_path_get_node(neo4j_value_t value, unsigned int hops)
{
    REQUIRE(neo4j_type(value) == NEO4J_PATH, neo4j_null);
    const struct neo4j_struct *v = (const struct neo4j_struct *)&value;
    assert(v->nfields == 3);
    assert(neo4j_type(v->fields[0]) == NEO4J_LIST);
    assert(neo4j_type(v->fields[2]) == NEO4J_LIST);

    const struct neo4j_list *nodes = (const struct neo4j_list *)&(v->fields[0]);
    const struct neo4j_list *seq = (const struct neo4j_list *)&(v->fields[2]);
    assert((seq->length % 2) == 0);

    if (hops > (seq->length / 2))
    {
        return neo4j_null;
    }

    if (hops == 0)
    {
        assert(nodes->length > 0 && neo4j_type(nodes->items[0]) == NEO4J_NODE);
        return nodes->items[0];
    }

    unsigned int seq_idx = ((hops - 1) * 2) + 1;
    assert(seq_idx < seq->length);
    assert(neo4j_type(seq->items[seq_idx]) == NEO4J_INT);
    const struct neo4j_int *node_idx =
        (const struct neo4j_int *)&(seq->items[seq_idx]);
    assert(node_idx->value >= 0 && node_idx->value < nodes->length);
    assert(neo4j_type(nodes->items[node_idx->value]) == NEO4J_NODE);
    return nodes->items[node_idx->value];
}


neo4j_value_t neo4j_path_get_relationship(neo4j_value_t value,
        unsigned int hops, bool *forward)
{
    REQUIRE(neo4j_type(value) == NEO4J_PATH, neo4j_null);
    ENSURE_NOT_NULL(bool, forward, false);
    const struct neo4j_struct *v = (const struct neo4j_struct *)&value;
    assert(v->nfields == 3);
    assert(neo4j_type(v->fields[1]) == NEO4J_LIST);
    assert(neo4j_type(v->fields[2]) == NEO4J_LIST);

    const struct neo4j_list *rels = (const struct neo4j_list *)&(v->fields[1]);
    const struct neo4j_list *seq = (const struct neo4j_list *)&(v->fields[2]);
    assert((seq->length % 2) == 0);

    if (hops > (seq->length / 2))
    {
        return neo4j_null;
    }

    unsigned int seq_idx = hops * 2;
    assert(seq_idx < seq->length);
    assert(neo4j_type(seq->items[seq_idx]) == NEO4J_INT);
    const struct neo4j_int *rel_idx =
        (const struct neo4j_int *)&(seq->items[seq_idx]);
    assert((rel_idx->value > 0 && rel_idx->value <= rels->length) ||
        (rel_idx->value < 0 && -(rel_idx->value) <= rels->length));
    *forward = (rel_idx->value > 0);
    unsigned int idx = (unsigned int)(llabs(rel_idx->value) - 1);
    assert(neo4j_type(rels->items[idx]) == NEO4J_RELATIONSHIP);
    return rels->items[idx];
}


// identity

neo4j_value_t neo4j_identity(long long value)
{
    if (value < 0)
    {
        return neo4j_null;
    }
#if LLONG_MIN != INT64_MIN || LLONG_MAX != INT64_MAX
    if (value > INT64_MAX)
    {
        return neo4j_null;
    }
#endif
    struct neo4j_int v =
        { ._type = NEO4J_IDENTITY, ._vt_off = IDENTITY_VT_OFF, .value = value };
    return *((neo4j_value_t *)(&v));
}


// struct

neo4j_value_t neo4j_struct(uint8_t signature,
        const neo4j_value_t *fields, uint16_t n)
{
    struct neo4j_struct v =
            { ._type = NEO4J_STRUCT, ._vt_off = STRUCT_VT_OFF,
              .signature = signature, .fields = fields, .nfields = n };
    return *((neo4j_value_t *)(&v));
}


bool struct_eq(const neo4j_value_t *value, const neo4j_value_t *other)
{
    const struct neo4j_struct *v = (const struct neo4j_struct *)value;
    const struct neo4j_struct *o = (const struct neo4j_struct *)other;

    if (v->signature != o->signature)
    {
        return false;
    }
    if (v->nfields != o->nfields)
    {
        return false;
    }

    for (unsigned int i = 0; i < v->nfields; ++i)
    {
        if (!neo4j_eq(v->fields[i], o->fields[i]))
        {
            return false;
        }
    }
    return true;
}
