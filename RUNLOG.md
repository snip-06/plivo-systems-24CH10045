# Run Log

| Experiment | Profile | `delay_ms` | Duration | Miss % | Overhead | NACK bytes | K | Result | Notes & Changes Made |
|------------|---------|------------|----------|--------|----------|------------|---|--------|----------------------|
| 1 | A | 60 | 5s | 5.60% | 1.93x | 0 | 3 | INVALID | Initial FEC with K=3. Backup arrives at ~60ms, no jitter headroom. |
| 2 | A | 60 | 5s | 1.20% | 1.93x | 0 | 2 | INVALID | Lowered K to 2. Better but still over 1% cap due to jitter. |
| 3 | A | 60 | 5s | 0.80% | 1.93x | 2 | 1 | VALID | Lowered K to 1 and enabled ARQ. First passing run! |
| 4 | B | 60 | 5s | 30.80% | 1.93x | 18 | 1 | INVALID | Profile B drops 18 packets. 60ms too tight for this profile. |
| 5 | B | 120 | 30s | <1% | 1.93x | 18 | 1 | VALID | Full 30s run. ARQ rescues burst drops, miss% diluted over 1500 frames. |
| 6 | B | 100 | 30s | <1% | 1.93x | 18 | 1 | VALID | Pushed delay down. 100ms is the lowest valid delay for Profile B. |
| 7 | B | 80 | 30s | >1% | 1.93x | 18 | 1 | INVALID | Too tight for Profile B's network latency. |
| 8 | A | 60 | 30s | <1% | 1.93x | ~2 | 1 | VALID | Final verification for Profile A at full duration. |
