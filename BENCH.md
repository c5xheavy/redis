# Benchmark history

Release build only (`-O3 -DNDEBUG`); numbers from Asan/Tsan/Debug builds are numbers about the sanitizer.
Same machine, same conditions, loopback. p99.9 is the first percentile line >= 99.9 in redis-benchmark's distribution.

| date | commit | test | rps | p50 ms | p99 ms | p99.9 ms | max ms | note |
|---|---|---|---|---|---|---|---|---|
| 2026-08-27 17:47 | 69ac115 | PING_INLINE | 189573 | 0.135 | 0.231 | 0.431 | 1.199 | baseline: epoll LT loop, one +PONG per recv, no input/output buffers |
| 2026-08-27 17:47 | 69ac115 | PING_MBULK | 199800 | 0.127 | 0.231 | 0.327 | 0.719 | baseline: epoll LT loop, one +PONG per recv, no input/output buffers |
| 2026-08-27 18:54 | 0cee8a4 | PING_INLINE | 204918 | 0.119 | 0.231 | 0.447 | 1.391 | per-connection state (connection* in epoll_data.ptr); recv/send path unchanged |
| 2026-08-27 18:54 | 0cee8a4 | PING_MBULK | 212766 | 0.119 | 0.215 | 0.295 | 0.423 | per-connection state (connection* in epoll_data.ptr); recv/send path unchanged |
| 2026-09-02 13:38 | fc7dabc | PING_INLINE | 215983 | 0.119 | 0.247 | 0.527 | 0.823 | input deque buffer + map::at lookup on recv path; since 0cee8a4: 27 commits |
| 2026-09-02 13:38 | fc7dabc | PING_MBULK | 207900 | 0.047 | 0.103 | 0.223 | 0.799 | input deque buffer + map::at lookup on recv path; since 0cee8a4: 27 commits |
| 2026-09-03 12:14 | 4d9c88d | PING_INLINE | 214133 | 0.119 | 0.263 | 0.415 | 0.671 | RESP state-machine parser on the hot path (deque line/byte reads per command); since fc7dabc: 9 commits |
| 2026-09-03 12:14 | 4d9c88d | PING_MBULK | 217628 | 0.111 | 0.183 | 0.359 | 0.615 | RESP state-machine parser on the hot path (deque line/byte reads per command); since fc7dabc: 9 commits |
| 2026-09-20 13:00 | 90d2c0e | PING_INLINE | 215517 | 0.119 | 0.223 | 0.423 | 0.783 | unique_fd everywhere, throw on startup, spare fd + MOD listen on every close, -ERR unknown command, case-insensitive command names; hot path: one extra str_tolower copy per command; since 4d9c88d: 64 commits |
| 2026-09-20 13:00 | 90d2c0e | PING_MBULK | 220022 | 0.103 | 0.151 | 0.255 | 0.503 | unique_fd everywhere, throw on startup, spare fd + MOD listen on every close, -ERR unknown command, case-insensitive command names; hot path: one extra str_tolower copy per command; since 4d9c88d: 64 commits |
