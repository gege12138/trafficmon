#include "protocol_db.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TM_PROTOCOL_LINE_MAX = 255 };

void tm_protocol_db_init(tm_protocol_db *database)
{
    if (database == NULL) {
        return;
    }
    memset(database, 0, sizeof(*database));
    memcpy(database->names[TM_PROTOCOL_OTHER], "Other", sizeof("Other"));
    database->protocol_count = 1;
}

static uint16_t tm_protocol_id_for_name(const tm_protocol_db *database, const char *name)
{
    size_t index;
    for (index = 1; index < database->protocol_count; index++) {
        if (strcmp(database->names[index], name) == 0) {
            return (uint16_t)index;
        }
    }
    return TM_PROTOCOL_MAX;
}

bool tm_protocol_db_add(tm_protocol_db *database, const char *name, uint8_t l4_proto,
                        uint16_t port)
{
    uint16_t protocol_id;
    size_t name_length;

    if (database == NULL || name == NULL || name[0] == '\0' ||
        database->rule_count >= TM_PROTOCOL_RULE_MAX) {
        return false;
    }
    name_length = strlen(name);
    if (name_length > TM_PROTOCOL_NAME_MAX) {
        return false;
    }

    protocol_id = tm_protocol_id_for_name(database, name);
    if (protocol_id == TM_PROTOCOL_MAX) {
        if (database->protocol_count >= TM_PROTOCOL_MAX) {
            return false;
        }
        protocol_id = (uint16_t)database->protocol_count;
        memcpy(database->names[protocol_id], name, name_length + 1);
        database->protocol_count++;
    }
    database->rules[database->rule_count] =
        (tm_protocol_rule){l4_proto, port, protocol_id};
    database->rule_count++;
    return true;
}

bool tm_protocol_db_add_with_id(tm_protocol_db *database, uint16_t protocol_id,
                                const char *name, uint8_t l4_proto, uint16_t port)
{
    size_t index;
    size_t name_length;
    if (database == NULL || name == NULL || name[0] == '\0' || protocol_id == 0 ||
        protocol_id >= TM_PROTOCOL_MAX || database->rule_count >= TM_PROTOCOL_RULE_MAX) {
        return false;
    }
    name_length = strlen(name);
    if (name_length > TM_PROTOCOL_NAME_MAX) {
        return false;
    }
    for (index = 1; index < TM_PROTOCOL_MAX; index++) {
        if (database->names[index][0] != '\0' &&
            ((index == protocol_id && strcmp(database->names[index], name) != 0) ||
             (index != protocol_id && strcmp(database->names[index], name) == 0))) {
            return false;
        }
    }
    if (database->names[protocol_id][0] == '\0') {
        memcpy(database->names[protocol_id], name, name_length + 1);
        if (database->protocol_count <= protocol_id) {
            database->protocol_count = (size_t)protocol_id + 1;
        }
    }
    for (index = 0; index < database->rule_count; index++) {
        if (database->rules[index].l4_proto == l4_proto &&
            database->rules[index].port == port) {
            return false;
        }
    }
    database->rules[database->rule_count++] =
        (tm_protocol_rule){l4_proto, port, protocol_id};
    return true;
}

static bool tm_parse_l4_protocol(const char *text, uint8_t *protocol)
{
    char *end = NULL;
    unsigned long value;
    if (strcmp(text, "tcp") == 0) {
        *protocol = 6;
        return true;
    }
    if (strcmp(text, "udp") == 0) {
        *protocol = 17;
        return true;
    }
    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > UINT8_MAX) {
        return false;
    }
    *protocol = (uint8_t)value;
    return true;
}

bool tm_protocol_db_load(tm_protocol_db *database, const char *path)
{
    FILE *stream;
    char line[TM_PROTOCOL_LINE_MAX + 2];
    tm_protocol_db loaded;

    if (database == NULL || path == NULL || (stream = fopen(path, "r")) == NULL) {
        return false;
    }
    tm_protocol_db_init(&loaded);
    while (fgets(line, sizeof(line), stream) != NULL) {
        char name[TM_PROTOCOL_NAME_MAX + 1];
        char protocol_text[16];
        unsigned int protocol_id;
        unsigned int port;
        uint8_t protocol;
        char extra;
        char *cursor = line;
        size_t length = strlen(line);

        if (length != 0 && line[length - 1] != '\n' && !feof(stream)) {
            fclose(stream);
            return false;
        }
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0' || *cursor == '\n' || *cursor == '#') {
            continue;
        }
        if (sscanf(cursor, "%u %31s %15s %u %c", &protocol_id, name, protocol_text, &port,
                   &extra) != 4 ||
            protocol_id == 0 || protocol_id >= TM_PROTOCOL_MAX || port > UINT16_MAX ||
            !tm_parse_l4_protocol(protocol_text, &protocol) ||
            !tm_protocol_db_add_with_id(&loaded, (uint16_t)protocol_id, name, protocol,
                                        (uint16_t)port)) {
            fclose(stream);
            return false;
        }
    }
    if (ferror(stream)) {
        fclose(stream);
        return false;
    }
    fclose(stream);
    *database = loaded;
    return true;
}

uint16_t tm_protocol_db_lookup(const tm_protocol_db *database, uint8_t l4_proto, uint16_t port)
{
    size_t index;
    if (database == NULL) {
        return TM_PROTOCOL_OTHER;
    }
    for (index = 0; index < database->rule_count; index++) {
        if (database->rules[index].l4_proto == l4_proto && database->rules[index].port == port) {
            return database->rules[index].protocol_id;
        }
    }
    return TM_PROTOCOL_OTHER;
}

const char *tm_protocol_db_name(const tm_protocol_db *database, uint16_t protocol_id)
{
    if (database == NULL || protocol_id >= database->protocol_count ||
        database->names[protocol_id][0] == '\0') {
        return "Other";
    }
    return database->names[protocol_id];
}

static void tm_fingerprint_byte(uint64_t *fingerprint, uint8_t byte)
{
    *fingerprint ^= byte;
    *fingerprint *= UINT64_C(1099511628211);
}

static int tm_rule_compare(const void *left_pointer, const void *right_pointer)
{
    const tm_protocol_rule *left = left_pointer;
    const tm_protocol_rule *right = right_pointer;
    if (left->l4_proto != right->l4_proto) {
        return left->l4_proto < right->l4_proto ? -1 : 1;
    }
    if (left->port != right->port) {
        return left->port < right->port ? -1 : 1;
    }
    return left->protocol_id < right->protocol_id ? -1 : left->protocol_id > right->protocol_id;
}

uint64_t tm_protocol_db_fingerprint(const tm_protocol_db *database)
{
    uint64_t fingerprint = UINT64_C(14695981039346656037);
    tm_protocol_rule sorted[TM_PROTOCOL_RULE_MAX];
    uint16_t protocol_id;
    if (database == NULL) {
        return 0;
    }
    for (protocol_id = 0; protocol_id < TM_PROTOCOL_MAX; protocol_id++) {
        const char *name = database->names[protocol_id];
        size_t index;
        if (name[0] == '\0') {
            continue;
        }
        tm_fingerprint_byte(&fingerprint, (uint8_t)protocol_id);
        for (index = 0; name[index] != '\0'; index++) {
            tm_fingerprint_byte(&fingerprint, (uint8_t)name[index]);
        }
        tm_fingerprint_byte(&fingerprint, 0);
    }
    memcpy(sorted, database->rules, database->rule_count * sizeof(*sorted));
    qsort(sorted, database->rule_count, sizeof(*sorted), tm_rule_compare);
    for (size_t index = 0; index < database->rule_count; index++) {
        const tm_protocol_rule *rule = &sorted[index];
        tm_fingerprint_byte(&fingerprint, rule->l4_proto);
        tm_fingerprint_byte(&fingerprint, (uint8_t)(rule->port >> 8));
        tm_fingerprint_byte(&fingerprint, (uint8_t)rule->port);
        tm_fingerprint_byte(&fingerprint, (uint8_t)rule->protocol_id);
    }
    return fingerprint == 0 ? UINT64_MAX : fingerprint;
}
