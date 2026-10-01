#ifndef TRAFFICMON_DEVICE_QUERY_H
#define TRAFFICMON_DEVICE_QUERY_H

#include "aggregator.h"
#include "identity.h"
#include "protocol_db.h"
#include "query.h"
#include "text_buffer.h"

#include <stdbool.h>

bool tm_device_query_render(const tm_aggregator *source, const tm_query_spec *spec,
                            tm_identity *identity, bool resolve_neighbors,
                            const tm_protocol_db *protocols, const char *period, bool json,
                            const char *filter, bool exact,
                            tm_text_buffer *output);

#endif
