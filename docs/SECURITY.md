# Security notes

## Threat model

`midi-sequencer` is a pure C++ library without I/O and works within the following boundaries:

- **Supply chain**: Dependencies are limited to the C++ standard library. CI uses pinned commit SHAs.
- **Memory safety**: Validates input using explicit, documented exceptions (`MidiError`) rather than undefined behavior. Code is hardened against buffer issues using `std::span` and `std::vector` instead of raw pointers.
- **Limits (Defended)**: The library enforces the following resource limits to prevent denial-of-service via malformed files:
  - `max_input_bytes`: 16,777,216
  - `max_tracks`: 65,535
  - `max_events_per_track`: 1,000,000
  - `max_event_bytes`: 1,048,576

## Not defended

- The library does not sanitize output; it serializes the provided `MidiFile` structure. The caller is responsible for the integrity of the data passed to `write_midi`.
- Strict validation (e.g., rejecting missing End-of-Track meta events) is out of scope; the library preserves the input structure.