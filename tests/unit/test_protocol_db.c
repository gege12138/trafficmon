#include "protocol_db.h"
#include "test.h"

#include <string.h>

void test_protocol_database(void)
{
    tm_protocol_db database;
    tm_protocol_db reordered;
    uint16_t dns_tcp;
    uint16_t dns_udp;

    tm_protocol_db_init(&database);
    TM_EXPECT_TRUE(tm_protocol_db_load(&database, TM_TEST_SOURCE_DIR "/fixtures/protocols.txt"));
    TM_EXPECT_TRUE(strcmp(tm_protocol_db_name(&database, TM_PROTOCOL_OTHER), "Other") == 0);
    TM_EXPECT_TRUE(strcmp(tm_protocol_db_name(&database, tm_protocol_db_lookup(&database, 6, 443)),
                          "HTTPS") == 0);
    dns_tcp = tm_protocol_db_lookup(&database, 6, 53);
    dns_udp = tm_protocol_db_lookup(&database, 17, 53);
    TM_EXPECT_EQ_U64(dns_tcp, dns_udp);
    TM_EXPECT_TRUE(strcmp(tm_protocol_db_name(&database, dns_tcp), "DNS") == 0);
    TM_EXPECT_EQ_U64(TM_PROTOCOL_OTHER, tm_protocol_db_lookup(&database, 6, 65000));
    TM_EXPECT_EQ_U64(20, tm_protocol_db_lookup(&database, 6, 443));
    TM_EXPECT_EQ_U64(30, dns_tcp);

    tm_protocol_db_init(&reordered);
    TM_EXPECT_TRUE(tm_protocol_db_add_with_id(&reordered, 40, "NTP", 17, 123));
    TM_EXPECT_TRUE(tm_protocol_db_add_with_id(&reordered, 30, "DNS", 17, 53));
    TM_EXPECT_TRUE(tm_protocol_db_add_with_id(&reordered, 20, "HTTPS", 6, 443));
    TM_EXPECT_TRUE(tm_protocol_db_add_with_id(&reordered, 30, "DNS", 6, 53));
    TM_EXPECT_TRUE(tm_protocol_db_add_with_id(&reordered, 10, "HTTP", 6, 80));
    TM_EXPECT_EQ_U64(tm_protocol_db_fingerprint(&database),
                     tm_protocol_db_fingerprint(&reordered));
    TM_EXPECT_TRUE(!tm_protocol_db_add_with_id(&reordered, 20, "Different", 6, 8443));
    TM_EXPECT_TRUE(!tm_protocol_db_add_with_id(&reordered, 21, "HTTPS", 6, 8443));
}
