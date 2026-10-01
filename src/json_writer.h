#ifndef TRAFFICMON_JSON_WRITER_H
#define TRAFFICMON_JSON_WRITER_H

#include "ipc_protocol.h"
#include "text_buffer.h"

#include <stdbool.h>

bool tm_json_write_query(const tm_ipc_query_response *response, tm_text_buffer *output);

#endif
