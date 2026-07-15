# Notes

### Recommended Settings
- **Profile A:** `delay_ms = 60` — FEC K=1 ensures backup arrives at 20ms, leaving 40ms jitter headroom.
- **Profile B:** `delay_ms = 100` — heavier loss and latency require a wider window for ARQ round-trips.

### Design Summary
The solution uses a hybrid FEC + ARQ architecture governed by a token bucket rate limiter. Every outgoing packet bundles the current frame plus a redundant copy of the previous frame (K=1 FEC offset), so single-frame drops are recovered instantly without any round-trip. For burst drops where both the original and its FEC backup are lost, the receiver detects gaps and sends 2-byte NACKs back to the sender requesting retransmission. The sender queues NACKs in a FIFO and services one per frame tick, slotting the requested frame into the FEC payload position. Sequence numbers are compressed to 16 bits and FEC offsets to 1 byte to stay well under the 2.0x bandwidth cap. The token bucket earns 310 bytes per frame and caps at 1000, allowing graceful degradation to small 162-byte packets when budget is tight. NACK retries are capped at 2 on both sides to prevent infinite loops.

### Known Failure Modes
1. **Burst drops longer than 2 frames** — FEC only covers 1 frame back, and ARQ needs ~40ms round-trip, so burst-3+ drops near the deadline boundary may not recover in time.
2. **Bandwidth starvation under extreme loss** — NACKs are serviced FIFO at 1 per frame tick, so a flood of NACKs will queue up and some may expire before being serviced.
3. **Tail-of-stream losses** — frames dropped in the last ~100ms of a run cannot be recovered because the harness kills the binaries before ARQ completes.
