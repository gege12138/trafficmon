#include "csv_writer.h"
#include "json_writer.h"
#include "subnet.h"
#include "test.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void test_writers(void)
{
    tm_ipc_query_response response;
    tm_ipc_query_row row;
    tm_text_buffer output;
    memset(&row, 0, sizeof(row));
    row.row.key.family = TM_FAMILY_IPV4;
    row.row.key.mac_known = true;
    row.row.key.mac[0] = 0xaa;
    row.row.key.mac[5] = 0xff;
    row.row.connections = UINT64_MAX;
    row.row.rx_bytes = UINT64_MAX;
    row.row.tx_bytes = 1;
    memcpy(row.protocol_name, "quote,\"line\n", sizeof("quote,\"line\n"));
    TM_EXPECT_TRUE(tm_ip_parse(TM_FAMILY_IPV4, "192.0.2.1", &row.row.key.host_ip));
    response.period_start = 20260901;
    response.period_end = 20261001;
    response.rows = &row;
    response.count = 1;

    TM_EXPECT_TRUE(tm_json_write_query(&response, &output));
    TM_EXPECT_TRUE(strstr(output.data, "18446744073709551615") != NULL);
    TM_EXPECT_TRUE(strstr(output.data, "quote,\\\"line\\n") != NULL);
    tm_text_buffer_destroy(&output);

    TM_EXPECT_TRUE(tm_csv_write_query(&response, &output));
    TM_EXPECT_TRUE(strstr(output.data, "\"quote,\"\"line\n\"") != NULL);
    TM_EXPECT_TRUE(strstr(output.data, "18446744073709551615") != NULL);
    tm_text_buffer_destroy(&output);
}
