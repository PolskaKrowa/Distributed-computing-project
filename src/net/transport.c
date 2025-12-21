/*
 * Transport layer dispatcher
 * 
 * Routes transport_init() calls to the appropriate implementation
 * (MPI or ZeroMQ) based on the configuration.
 */

#include "transport.h"
#include "../common/log.h"

#include <stdlib.h>
#include <string.h>

/* Forward declarations of implementation-specific init functions */
extern int transport_init_mpi(transport_t **out, const transport_config_t *config);
extern int transport_init_zmq(transport_t **out, const transport_config_t *config);

/*
 * Dispatcher for transport initialization
 */
int transport_init(transport_t **out, const transport_config_t *config)
{
    if (!out || !config) {
        log_error("transport_init: NULL argument");
        return -1;
    }

    switch (config->type) {
        case TRANSPORT_TYPE_MPI:
            return transport_init_mpi(out, config);
        case TRANSPORT_TYPE_ZMQ:
            return transport_init_zmq(out, config);
        default:
            log_error("transport_init: unknown transport type %d", config->type);
            return -2;
    }
}

/*
 * Convert transport type to string
 */
const char *transport_type_to_string(transport_type_t type)
{
    switch (type) {
        case TRANSPORT_TYPE_MPI:
            return "MPI";
        case TRANSPORT_TYPE_ZMQ:
            return "ZeroMQ";
        default:
            return "Unknown";
    }
}

/*
 * Convert message type to string
 */
const char *message_type_to_string(message_type_t type)
{
    switch (type) {
        case MSG_TYPE_HEARTBEAT:
            return "HEARTBEAT";
        case MSG_TYPE_TASK_SUBMIT:
            return "TASK_SUBMIT";
        case MSG_TYPE_TASK_RESULT:
            return "TASK_RESULT";
        case MSG_TYPE_TASK_CANCEL:
            return "TASK_CANCEL";
        case MSG_TYPE_WORKER_REGISTER:
            return "WORKER_REGISTER";
        case MSG_TYPE_WORKER_SHUTDOWN:
            return "WORKER_SHUTDOWN";
        case MSG_TYPE_COORDINATOR_CMD:
            return "COORDINATOR_CMD";
        case MSG_TYPE_ERROR:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}
