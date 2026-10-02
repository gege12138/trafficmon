#include "test.h"

unsigned int tm_test_failures = 0;

void test_flow_types(void);
void test_subnets(void);
void test_writers(void);
#ifdef TM_HAVE_CONNTRACK_TESTS
void test_conntrack_parser(void);
#endif
void test_protocol_database(void);
void test_aggregator(void);
void test_database(void);
void test_exporter(void);
void test_ipc_protocol(void);
void test_query(void);
void test_identity(void);
void test_journal(void);
void test_period(void);
void test_period_store(void);
void test_retention(void);
#ifdef TM_HAVE_SMB_TESTS
void test_smb(void);
#endif
void test_oracle_vectors(void);

int main(void)
{
    const tm_test_case tests[] = {
        {"flow types", test_flow_types},
        {"subnets", test_subnets},
        {"writers", test_writers},
#ifdef TM_HAVE_CONNTRACK_TESTS
        {"conntrack parser", test_conntrack_parser},
#endif
        {"protocol database", test_protocol_database},
        {"aggregator", test_aggregator},
        {"database", test_database},
        {"exporter", test_exporter},
        {"ipc protocol", test_ipc_protocol},
        {"query", test_query},
        {"identity", test_identity},
        {"journal", test_journal},
        {"period", test_period},
        {"period store", test_period_store},
        {"retention", test_retention},
#ifdef TM_HAVE_SMB_TESTS
        {"smb", test_smb},
#endif
        {"oracle vectors", test_oracle_vectors},
    };
    const size_t test_count = sizeof(tests) / sizeof(tests[0]);

    for (size_t index = 0; index < test_count; index++) {
        unsigned int failures_before = tm_test_failures;
        tests[index].function();
        printf("%s %s\n", failures_before == tm_test_failures ? "PASS" : "FAIL", tests[index].name);
    }

    printf("%zu tests, %u failures\n", test_count, tm_test_failures);
    return tm_test_failures == 0 ? 0 : 1;
}
