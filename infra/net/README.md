# Network Module

## Active Components

- `redis_client.h/cpp` — Redis client with circuit breaker and exponential backoff
- `distributed_session_store.h/cpp` — Multi-instance session management via Redis
- `health_probe.h` — Health check utilities

## Build

```cmake
# poker_net links: Redis client + session store
target_link_libraries(my_target PRIVATE poker_net)
```

## Deprecation

The legacy WebSocket server implementation has been moved to `../../legacy/net/`. 
The canonical WebSocket server is in `phase13/`.

See also: `../CLAUDE.md` for project build instructions.
