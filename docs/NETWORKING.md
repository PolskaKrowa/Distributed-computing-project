# Transport Layer Implementation

This directory contains the networking/transport layer for the distributed computing platform. The transport layer provides a clean abstraction for message passing between coordinators and workers, supporting both tightly-coupled HPC clusters (MPI) and loosely-coupled volunteer computing (ZeroMQ).

---

## Design Goals

1. **Transport independence**: Coordinator and worker code shouldn't know which transport is used
2. **Message reliability**: Guaranteed delivery with proper error handling
3. **Efficiency**: Minimal overhead for both small and large payloads
4. **Simplicity**: Clear API that's easy to integrate
5. **Scalability**: Support from single-machine testing to hundreds of workers

---

## Files

- **`transport.h`**: Public API and message definitions
- **`transport_mpi.c`**: MPI-based transport for HPC clusters
- **`transport_zmq.c`**: ZeroMQ-based transport for volunteer computing
- **`transport_example.c`**: Example usage and test programme

---

## Architecture

### Message Structure

All messages use a fixed-size header followed by an optional payload:

```c
message_header_t {
    uint32_t magic;         // 0xDA7A1E00
    uint16_t version;       // Envelope version (currently 1)
    uint16_t msg_type;      // Message type (see below)
    uint64_t task_id;       // Task identifier
    uint32_t payload_len;   // Payload size in bytes
}
```

### Message Types

- **`MSG_TYPE_HEARTBEAT`**: Worker keep-alive signal
- **`MSG_TYPE_TASK_SUBMIT`**: Coordinator assigns task to worker
- **`MSG_TYPE_TASK_RESULT`**: Worker returns result to coordinator
- **`MSG_TYPE_TASK_CANCEL`**: Coordinator cancels a task
- **`MSG_TYPE_WORKER_REGISTER`**: Worker joins the pool
- **`MSG_TYPE_WORKER_SHUTDOWN`**: Worker leaves the pool
- **`MSG_TYPE_COORDINATOR_CMD`**: Control commands
- **`MSG_TYPE_ERROR`**: Error notifications

### Transport Implementations

#### MPI Transport (`transport_mpi.c`)

**Use case**: Tightly-coupled HPC clusters with MPI installed

**Characteristics**:
- Uses `MPI_COMM_WORLD` for all communication
- Rank 0 is always the coordinator
- Ranks 1-N are workers
- Point-to-point messaging with `MPI_Send`/`MPI_Recv`
- Efficient for low-latency, high-bandwidth networks
- Workers must be launched together (e.g., `mpirun -np 8`)

**Topology**:
```
Coordinator (rank 0)
    ├── Worker (rank 1)
    ├── Worker (rank 2)
    └── Worker (rank 3)
```

**Example**:
```bash
mpirun -np 4 ./coordinator_program
```

#### ZeroMQ Transport (`transport_zmq.c`)

**Use case**: Volunteer computing, dynamic worker pools, distributed networks

**Characteristics**:
- Coordinator uses `ROUTER` socket (binds to endpoint)
- Workers use `DEALER` sockets (connect to endpoint)
- Dynamic worker registration/deregistration
- Each worker generates unique identity
- Supports NAT traversal and firewall-friendly topologies
- Workers can join/leave at any time

**Topology**:
```
Coordinator (ROUTER @ tcp://host:5555)
    ├── Worker 1 (DEALER)
    ├── Worker 2 (DEALER)
    └── Worker N (DEALER)
```

**Example**:
```bash
# Terminal 1: Start coordinator
./coordinator tcp://0.0.0.0:5555

# Terminal 2-N: Start workers (can join anytime)
./worker tcp://coordinator-host:5555
```

---

## API Usage

### Initialisation

```c
#include "transport.h"
#include "../common/log.h"

// Configure transport
transport_config_t config = {
    .type = TRANSPORT_TYPE_ZMQ,  // or TRANSPORT_TYPE_MPI
    .role = TRANSPORT_ROLE_COORDINATOR,  // or TRANSPORT_ROLE_WORKER
    .endpoint = "tcp://127.0.0.1:5555",  // ZMQ only
    .timeout_ms = 5000,
    .max_workers = 64
};

// Initialise
transport_t *transport;
if (transport_init(&transport, &config) != 0) {
    log_fatal("Transport initialisation failed");
}
```

### Sending Messages

```c
// Allocate message with payload space
message_t *msg = message_alloc(payload_size);

// Fill payload
memcpy(msg->payload, data, payload_size);

// Set header
message_set_header(msg, MSG_TYPE_TASK_SUBMIT, task_id);
msg->header.payload_len = payload_size;

// Send to worker 0
if (transport_send(transport, msg, 0) != 0) {
    log_error("Failed to send message");
}

message_free(msg);
```

### Receiving Messages

```c
int src_rank;
message_t *msg = transport_recv(transport, &src_rank);

if (!msg) {
    // Timeout or error
    return;
}

// Process based on type
switch (msg->header.msg_type) {
    case MSG_TYPE_TASK_RESULT:
        handle_result(msg->payload, msg->header.payload_len);
        break;
    case MSG_TYPE_HEARTBEAT:
        log_debug("Heartbeat from worker %d", src_rank);
        break;
    // ...
}

message_free(msg);
```

### Broadcasting (Coordinator Only)

```c
message_t *msg = message_alloc(0);
message_set_header(msg, MSG_TYPE_COORDINATOR_CMD, 0);

// Send to all active workers
if (transport_broadcast(transport, msg) != 0) {
    log_error("Broadcast failed");
}

message_free(msg);
```

### Polling for Messages

```c
// Check if message available (non-blocking)
int available = transport_poll(transport, 100 /* ms */);

if (available > 0) {
    message_t *msg = transport_recv(transport, NULL);
    // Process message...
}
```

### Shutdown

```c
transport_shutdown(transport);
```

---

## Integration with Coordinator/Worker

### Coordinator Pattern

```c
int main(void) {
    log_init("coordinator.log", LOG_LEVEL_DEBUG);
    
    // Initialise transport
    transport_t *t;
    transport_init(&t, &config);
    
    // Wait for workers
    int workers = transport_worker_count(t);
    log_info("Connected workers: %d", workers);
    
    // Distribute tasks
    for (each task) {
        int worker = select_worker();
        message_t *msg = create_task_message(task);
        transport_send(t, msg, worker);
        message_free(msg);
    }
    
    // Collect results
    while (results_pending) {
        int src;
        message_t *result = transport_recv(t, &src);
        process_result(result);
        message_free(result);
    }
    
    transport_shutdown(t);
    log_shutdown();
}
```

### Worker Pattern

```c
int main(void) {
    log_init("worker.log", LOG_LEVEL_DEBUG);
    
    // Initialise transport
    transport_t *t;
    transport_init(&t, &config);
    
    // Main loop
    while (running) {
        message_t *task = transport_recv(t, NULL);
        
        if (!task) {
            // Timeout - send heartbeat
            send_heartbeat(t);
            continue;
        }
        
        if (task->header.msg_type == MSG_TYPE_TASK_SUBMIT) {
            // Execute task
            result_t result = execute_task(task);
            
            // Send result
            message_t *reply = create_result_message(result);
            transport_send(t, reply, 0);
            message_free(reply);
        }
        
        message_free(task);
    }
    
    transport_shutdown(t);
    log_shutdown();
}
```

---

## Building

### MPI Version

```bash
# Requires MPI (e.g., OpenMPI or MPICH)
mpicc -o coordinator coordinator.c transport_mpi.c ../common/log.c ../common/errors.c
```

### ZeroMQ Version

```bash
# Requires libzmq-dev
gcc -o coordinator coordinator.c transport_zmq.c ../common/log.c ../common/errors.c -lzmq -lpthread
```

### CMake Integration

```cmake
# In CMakeLists.txt

# Find dependencies
find_package(MPI)
find_package(ZMQ)

# MPI transport
if(MPI_FOUND)
    add_library(transport_mpi src/net/transport_mpi.c)
    target_link_libraries(transport_mpi MPI::MPI_C common)
endif()

# ZeroMQ transport
if(ZMQ_FOUND)
    add_library(transport_zmq src/net/transport_zmq.c)
    target_link_libraries(transport_zmq ${ZMQ_LIBRARIES} pthread common)
endif()
```

---

## Testing

### Running the Example

**MPI**:
```bash
# Compile
mpicc transport_example.c transport_mpi.c ../common/log.c -o example_mpi

# Run with 4 processes (1 coordinator + 3 workers)
mpirun -np 4 ./example_mpi coordinator mpi
```

**ZeroMQ**:
```bash
# Compile
gcc transport_example.c transport_zmq.c ../common/log.c -lzmq -lpthread -o example_zmq

# Terminal 1: Coordinator
./example_zmq coordinator zmq tcp://127.0.0.1:5555

# Terminal 2-4: Workers (start in any order)
./example_zmq worker zmq tcp://127.0.0.1:5555
```

### Expected Output

Coordinator:
```
[2024-12-20 14:30:00] INFO  Starting coordinator (transport=ZeroMQ)
[2024-12-20 14:30:00] INFO  ZMQ coordinator bound to tcp://127.0.0.1:5555
[2024-12-20 14:30:02] INFO  Connected workers: 3
[2024-12-20 14:30:02] INFO  Distributing 10 tasks to 3 workers
[2024-12-20 14:30:02] INFO  Task 1 assigned to worker 0
...
[2024-12-20 14:30:05] INFO  Result from worker 0: task=1, result=0.999001
```

Worker:
```
[2024-12-20 14:30:01] INFO  Starting worker (transport=ZeroMQ)
[2024-12-20 14:30:01] INFO  Worker worker-12345-1734710401 connected
[2024-12-20 14:30:02] INFO  Processing task 1 (input=1.00, iterations=1000)
[2024-12-20 14:30:03] INFO  Result for task 1 sent
```

---

## Error Handling

The transport layer follows the project's error handling conventions:

- **Return codes**: 0 = success, negative = error
- **Logging**: All errors logged via `log.h`
- **No exceptions**: Pure C error handling
- **Timeouts**: Configurable per operation
- **Statistics**: Track errors via `transport_stats_t`

### Common Error Scenarios

1. **Connection failures** (ZMQ):
   - Check endpoint address
   - Verify firewall rules
   - Ensure coordinator started first

2. **MPI initialisation failures**:
   - Verify MPI installation
   - Check process count matches expectations
   - Ensure `mpirun` used for launching

3. **Message validation failures**:
   - Check magic number (0xDA7A1E00)
   - Verify payload size
   - Ensure version compatibility

4. **Timeout errors**:
   - Increase `timeout_ms` for slow networks
   - Check worker responsiveness
   - Monitor network latency

---

## Performance Considerations

### MPI

- **Latency**: <1μs for small messages on Infiniband
- **Bandwidth**: Near wire-speed (10-100 Gb/s)
- **Scalability**: Excellent up to 10,000+ processes
- **Best for**: HPC clusters, supercomputers, low-latency networks

### ZeroMQ

- **Latency**: 10-100μs for small messages
- **Bandwidth**: 1-10 Gb/s typical
- **Scalability**: Hundreds of workers
- **Best for**: Internet-scale deployments, volunteer computing, heterogeneous networks

### Optimisation Tips

1. **Batch small messages** to reduce overhead
2. **Reuse message objects** instead of frequent alloc/free
3. **Use appropriate timeout values** (longer for WAN)
4. **Monitor statistics** to identify bottlenecks
5. **Consider payload size** vs. message frequency trade-offs

---

## Thread Safety

- Transport handles are **NOT thread-safe**
- Use one transport per thread, OR
- Protect with mutex if shared between threads
- Message objects are safe to use concurrently if different instances

---

## Future Enhancements

Potential improvements for future versions:

1. **Compression**: Optional payload compression for large messages
2. **Encryption**: TLS/SSL support for ZeroMQ
3. **Multiplexing**: Multiple logical channels over one transport
4. **Flow control**: Back-pressure mechanisms
5. **Reliability**: Automatic retry with exponential backoff
6. **Monitoring**: Prometheus-style metrics export

---

## Troubleshooting

### "MPI_Init failed"
- Ensure MPI is properly installed
- Use `mpirun` to launch programmes
- Check MPI environment variables

### "zmq_bind failed: Address already in use"
- Another process is using the port
- Change endpoint port number
- Kill existing coordinator process

### "No workers available"
- For MPI: Ensure multiple processes launched (`-np N` where N > 1)
- For ZMQ: Start worker processes after coordinator
- Check logs for worker registration messages

### Messages timing out
- Increase `timeout_ms` in config
- Verify network connectivity
- Check coordinator/worker are using same endpoint
- For ZMQ: Verify workers can reach coordinator host

---

## References

- [MPI Standard](https://www.mpi-forum.org/)
- [ZeroMQ Guide](https://zguide.zeromq.org/)
- Project design docs: `docs/DESIGN.md`
- API contracts: `docs/INCLUDE_FILES.md`

---

For questions or issues with the transport layer, see the main project documentation or file an issue in the issue tracker.