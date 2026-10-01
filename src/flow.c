#include "flow.h"

bool tm_family_is_valid(uint8_t family)
{
    return family == TM_FAMILY_IPV4 || family == TM_FAMILY_IPV6;
}

size_t tm_ip_size(uint8_t family)
{
    if (family == TM_FAMILY_IPV4) {
        return 4;
    }
    if (family == TM_FAMILY_IPV6) {
        return TM_IPV6_LENGTH;
    }
    return 0;
}
