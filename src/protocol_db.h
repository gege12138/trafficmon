#ifndef TRAFFICMON_PROTOCOL_DB_H
#define TRAFFICMON_PROTOCOL_DB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TM_PROTOCOL_OTHER = 0,
    TM_PROTOCOL_NAME_MAX = 31,
    TM_PROTOCOL_MAX = 256,
    TM_PROTOCOL_RULE_MAX = 512,
};

typedef struct {
    uint8_t l4_proto;
    uint16_t port;
    uint16_t protocol_id;
} tm_protocol_rule;

typedef struct {
    char names[TM_PROTOCOL_MAX][TM_PROTOCOL_NAME_MAX + 1];
    tm_protocol_rule rules[TM_PROTOCOL_RULE_MAX];
    size_t protocol_count;
    size_t rule_count;
} tm_protocol_db;

void tm_protocol_db_init(tm_protocol_db *database);
bool tm_protocol_db_add(tm_protocol_db *database, const char *name, uint8_t l4_proto,
                        uint16_t port);
bool tm_protocol_db_add_with_id(tm_protocol_db *database, uint16_t protocol_id,
                                const char *name, uint8_t l4_proto, uint16_t port);
bool tm_protocol_db_load(tm_protocol_db *database, const char *path);
uint16_t tm_protocol_db_lookup(const tm_protocol_db *database, uint8_t l4_proto, uint16_t port);
const char *tm_protocol_db_name(const tm_protocol_db *database, uint16_t protocol_id);
uint64_t tm_protocol_db_fingerprint(const tm_protocol_db *database);

#endif
