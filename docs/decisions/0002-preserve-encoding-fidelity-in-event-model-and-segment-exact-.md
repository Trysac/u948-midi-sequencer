# Preserve Encoding Fidelity in Event Model and Segment-Exact Integer Tempo Integration

Status: accepted (generated, pending human review)

## Context

Standard MIDI File libraries often normalize running status, rewrite note-on velocity 0 as note-off, drop unrecognized chunks, and accumulate floating-point drift across tempo changes.

## Decision

Represent every event with its raw status byte, payload bytes, and a running_status boolean flag; record unknown chunks with their chunk index; and compute microsecond timestamps using 64-bit integer arithmetic segmented at each tempo change.

## Consequences

Valid MIDI files round-trip byte-for-byte through read_midi and write_midi, hand-edited models remain valid because write_midi validates running status transitions, and microsecond timing is deterministic and exact.
