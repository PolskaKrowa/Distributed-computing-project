# Runtime Module Documentation

The runtime module provides essential infrastructure for task management and resource control in the Distributed Computing Project.

## Overview

The runtime module consists of two main components:

1. **Task Queue** (`task_queue.h/c`) - Thread-safe task management with priority support
2. **Resource Limits** (`resource_limits.h/c`) - Resource monitoring and enforcement

Both components are designed to work together to provide reliable, efficient task execution.

## Building

### Requirements

- GCC or compatible C compiler
- POSIX threads (pthread)
- Linux operating system (for resource monitoring)

### Quick Build

```bash
# Build release version
make

# Build debug version with symbols
make debug

# Build and run example
make example

# Run tests
make test
```

### Manual Build

```bash
gcc -o runtime_example \
    examples/minimal_cluster/runtime_example.c \
    src/runtime/task_queue.c \
    src/runtime/resource_limits.c \
    src/common/log.c \
    src/common/errors.c \
    -I./include -I./src \
    -lpthread -lm \
    -Wall -Wextra
```

## Task Queue

### Overview

The task queue provides thread-safe FIFO task management with priority support. It's designed for producer-consumer patterns where coordinators enqueue tasks and workers dequeue them for execution.

### Key Features

- **Thread-safe operations** using mutexes and condition variables
- **Priority queuing** - high-priority tasks jump ahead
- **Blocking and non-blocking** dequeue operations
- **Configurable capacity** limits
- **Duplicate detection** (optional)
- **Statistics tracking** for monitoring

### Basic Usage

```c
#include "runtime/task_queue.h"

// Create queue
task_queue_config_t config = {
    .max_size = 1000,
    .allow_duplicates = 0
};
task_queue_t *queue = task_queue_create(&config);

// Enqueue task
task_t task = {
    .task_id = 1,
    .model_id = 100,
    .flags = TASK_FLAG_HIGH_PRIORITY,
    .timeout_secs = 60
};
task_queue_enqueue(queue, &task);

// Dequeue task (blocking with timeout)
task_t next_task;
int rc = task_queue_dequeue_wait(queue, &next_task, 5000);
if (rc == 0) {
    // Process task
}

// Cleanup
task_queue_destroy(queue);
```

### Priority Handling

Tasks with `TASK_FLAG_HIGH_PRIORITY` are automatically placed ahead of normal-priority tasks in the queue:

```c
task_t urgent_task = {
    .task_id = 42,
    .flags = TASK_FLAG_HIGH_PRIORITY,
    // ... other fields
};
task_queue_enqueue(queue, &urgent_task);
// This task will be dequeued before any normal-priority tasks
```

### Producer-Consumer Pattern

```c
// Producer thread
void *producer(void *arg) {
    task_queue_t *queue = (task_queue_t *)arg;
    
    for (int i = 0; i < num_tasks; i++) {
        task_t task = create_task(i);
        
        while (task_queue_enqueue(queue, &task) != 0) {
            usleep(10000);  // Queue full - wait
        }
    }
    return NULL;
}

// Worker thread
void *worker(void *arg) {
    task_queue_t *queue = (task_queue_t *)arg;
    
    while (running) {
        task_t task;
        int rc = task_queue_dequeue_wait(queue, &task, 1000);
        
        if (rc == 0) {
            process_task(&task);
        }
    }
    return NULL;
}
```

### Statistics

```c
task_queue_stats_t stats;
task_queue_get_stats(queue, &stats);

printf("Queue size: %zu\n", stats.current_size);
printf("High priority: %lu\n", stats.high_priority_count);
printf("Total enqueued: %lu\n", stats.total_enqueued);
printf("Total dequeued: %lu\n", stats.total_dequeued);
printf("Blocked threads: %lu\n", stats.blocked_threads);
```

### Task Cancellation

Remove specific tasks by ID:

```c
uint64_t task_id = 12345;
if (task_queue_remove_by_id(queue, task_id) == 0) {
    printf("Task %lu cancelled\n", task_id);
}
```

### Thread Safety

All queue operations are thread-safe. Multiple producers and consumers can safely access the same queue simultaneously.

## Resource Limits

### Overview

The resource limits module monitors and enforces resource usage constraints to prevent system exhaustion and ensure fair resource allocation.

### Key Features

- **Memory monitoring** (RSS, virtual, peak)
- **CPU usage tracking** (percentage and time)
- **Concurrency limits** (max simultaneous tasks)
- **Disk space checking**
- **File descriptor tracking**
- **Soft and hard limits** with warnings
- **Per-task resource reservation**

### Basic Usage

```c
#include "runtime/resource_limits.h"

// Create limiter
resource_limits_config_t config = {
    .max_memory_bytes = 2ULL * 1024 * 1024 * 1024,  // 2 GB
    .warn_memory_bytes = 1536ULL * 1024 * 1024,      // 1.5 GB warning
    .max_cpu_percent = 80.0,
    .max_concurrent_tasks = 8,
    .enforce_hard_limits = 1
};
resource_limits_t *limits = resource_limits_create(&config);

// Update resource measurements
resource_limits_update(limits);

// Check limits
limit_status_t status = resource_limits_check_all(limits);
if (status == LIMIT_WARNING) {
    printf("Warning: approaching resource limits\n");
} else if (status == LIMIT_EXCEEDED) {
    printf("Error: resource limit exceeded\n");
}

// Cleanup
resource_limits_destroy(limits);
```

### Resource Monitoring

Get current resource usage:

```c
resource_stats_t stats;
resource_limits_get_stats(limits, &stats);

printf("Memory (RSS): %.1f MB\n", stats.memory_rss_bytes / (1024.0 * 1024.0));
printf("Memory (Peak): %.1f MB\n", stats.memory_peak_bytes / (1024.0 * 1024.0));
printf("CPU usage: %.1f%%\n", stats.cpu_usage_percent);
printf("CPU time: %.2f sec\n", stats.cpu_time_us / 1000000.0);
printf("Threads: %d\n", stats.num_threads);
printf("Open files: %d\n", stats.open_files);
printf("Tasks completed: %lu\n", stats.tasks_completed);
```

### Task Resource Reservation

Reserve resources before executing tasks:

```c
// Before starting task
if (resource_limits_reserve_task(limits) == 0) {
    // Resources available - execute task
    execute_task(&task);
    
    // After completion
    resource_limits_release_task(limits, 1);  // 1 = success
} else {
    // Resources unavailable - defer task
    printf("Insufficient resources\n");
}
```

### Limit Checking

Check individual resources:

```c
// Check memory
limit_status_t mem_status = resource_limits_check(limits, RESOURCE_MEMORY);

// Check CPU
limit_status_t cpu_status = resource_limits_check(limits, RESOURCE_CPU_TIME);

// Check concurrent tasks
limit_status_t task_status = resource_limits_check(limits, RESOURCE_TASKS_RUNNING);

// Check all resources
limit_status_t overall = resource_limits_check_all(limits);
```

Status codes:

- `LIMIT_OK` - Within limits
- `LIMIT_WARNING` - Approaching limit (>80%)
- `LIMIT_EXCEEDED` - Hard limit exceeded
- `LIMIT_CRITICAL` - Immediate action required

### Dynamic Limit Adjustment

Adjust limits at runtime:

```c
// Set soft limit (warning threshold)
resource_limits_set_soft_limit(limits, RESOURCE_MEMORY,
                               1024ULL * 1024 * 1024);  // 1 GB

// Set hard limit (maximum)
resource_limits_set_hard_limit(limits, RESOURCE_MEMORY,
                               2ULL * 1024 * 1024 * 1024);  // 2 GB

// Adjust concurrency
resource_limits_set_hard_limit(limits, RESOURCE_TASKS_RUNNING, 16);
```

## Integration Example

Combining task queue and resource limits for a worker:

```c
task_queue_t *queue = task_queue_create(NULL);
resource_limits_t *limits = resource_limits_create(NULL);

while (running) {
    // Check resources before accepting work
    if (resource_limits_check_all(limits) >= LIMIT_EXCEEDED) {
        sleep(1);
        resource_limits_update(limits);
        continue;
    }
    
    // Reserve resources
    if (resource_limits_reserve_task(limits) != 0) {
        sleep(1);
        continue;
    }
    
    // Get next task
    task_t task;
    int rc = task_queue_dequeue_wait(queue, &task, 1000);
    
    if (rc == 0) {
        // Execute task
        int success = execute_task(&task);
        
        // Release resources
        resource_limits_release_task(limits, success);
    } else {
        // Timeout - release reservation
        resource_limits_release_task(limits, 0);
    }
}
```

## Performance Considerations

### Task Queue

- **Enqueue/Dequeue**: O(1) for normal priority, O(n) for high priority insertion
- **Search**: O(n) for removal by ID
- **Memory**: One allocation per task node
- **Contention**: Minimal - short critical sections

Optimisation tips:

1. Use reasonable queue sizes to avoid memory exhaustion
2. Minimise high-priority task count for best performance
3. Batch operations where possible
4. Use non-blocking dequeue when appropriate

### Resource Limits

- **Update frequency**: Call `resource_limits_update()` every 1-5 seconds
- **Check overhead**: Minimal for individual checks
- **Memory**: ~1 KB per limiter instance
- **I/O**: Reads `/proc/self/status` and `/proc/self/fd`

Optimisation tips:

1. Update resources periodically, not per-task
2. Cache limit check results for short periods
3. Use soft limits to avoid hard limit hits
4. Monitor only relevant resources

## Error Handling

Both modules use consistent error handling:

```c
// Check return codes
int rc = task_queue_enqueue(queue, &task);
if (rc == -1) {
    // General error
} else if (rc == -2) {
    // Queue full
} else if (rc == -3) {
    // Duplicate task ID
}

// Reserve operations
rc = resource_limits_reserve_task(limits);
if (rc == -1) {
    // Max concurrent reached
} else if (rc == -2) {
    // Memory too high
}
```

All errors are logged via the common logging system.

## Thread Safety

- **Task Queue**: Fully thread-safe for all operations
- **Resource Limits**: Thread-safe for all public operations

Both modules use mutexes internally. No external synchronisation is required when calling their functions.

## Testing

Run the example programme:

```bash
make example
```

Expected output:

```
=== Example 1: Basic Task Queue ===
Enqueued task 1 (priority=HIGH)
Enqueued task 2 (priority=NORMAL)
...

=== Example 2: Producer-Consumer Pattern ===
Producer 0 starting (tasks=10)
Worker 0 starting
...

=== Example 3: Resource Limits ===
Current resource usage:
  Memory (RSS): 12.5 MB
  CPU usage: 2.3%
...

=== All Examples Completed ===
```

## Integration with Other Modules

### With Transport Layer

```c
// Coordinator receives tasks from network
message_t *msg = transport_recv(transport, &src);
if (msg && msg->header.msg_type == MSG_TYPE_TASK_SUBMIT) {
    task_t *task = (task_t *)msg->payload;
    task_queue_enqueue(queue, task);
}
```

### With Fortran Kernels

```c
// Worker executes Fortran model
task_t task;
task_queue_dequeue(queue, &task);

// Check resources
if (resource_limits_reserve_task(limits) == 0) {
    int32_t status = fortran_model_run_v1(
        task.input, task.input_size,
        task.output, task.output_size,
        NULL, 0,
        task.trace_id,
        NULL
    );
    
    resource_limits_release_task(limits, status == 0);
}
```

## Troubleshooting

### Queue fills up

- Increase `max_size` in configuration
- Add more workers
- Check for slow task processing
- Monitor with `task_queue_get_stats()`

### Resource limits exceeded

- Increase limits if legitimate
- Check for memory leaks
- Reduce concurrent tasks
- Monitor with `resource_limits_get_stats()`

### High CPU usage

- Reduce `max_concurrent_tasks`
- Add sleep between iterations
- Check task complexity
- Use `resource_limits_update()` less frequently

### Blocked threads

- Check `task_queue_stats_t.blocked_threads`
- Ensure producers are running
- Use reasonable timeouts
- Call `task_queue_wake_all()` during shutdown

## Best Practises

1. **Always destroy queues and limiters** to free resources
2. **Check return codes** from all functions
3. **Update resource stats periodically** (1-5 second intervals)
4. **Use appropriate timeouts** for blocking operations
5. **Monitor statistics** for performance tuning
6. **Handle errors gracefully** and log issues
7. **Test with realistic workloads** before production
8. **Use soft limits** to avoid hard limit violations

## Further Reading

- See `docs/DESIGN.md` for architecture overview
- See `docs/INCLUDE_FILES.md` for API documentation
- See `examples/minimal_cluster/runtime_example.c` for complete examples
- See `include/task.h` for task structure definition

---

For questions or issues with the runtime modules, see the main project documentation or file an issue in the issue tracker.