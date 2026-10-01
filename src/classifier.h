#ifndef TRAFFICMON_CLASSIFIER_H
#define TRAFFICMON_CLASSIFIER_H

#include "flow.h"
#include "protocol_db.h"
#include "subnet.h"

#include <stdint.h>

typedef enum {
    TM_CLASSIFY_INVALID = -1,
    TM_CLASSIFY_IGNORED = 0,
    TM_CLASSIFY_ACCEPTED = 1,
} tm_classify_result;

typedef struct {
    tm_flow_delta delta;
    uint16_t protocol_id;
} tm_classified_delta;

tm_classify_result tm_classify_flow(const tm_raw_flow *flow, const tm_subnet_set *local_networks,
                                    const tm_protocol_db *protocols,
                                    tm_classified_delta *classified);

#endif
