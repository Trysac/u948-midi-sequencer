# Testing

## Test suites

- `tests/midi_test.cpp`:
  - **VLQ**: Verifies encoding/decoding matches SMF 1.0 spec table (0x0 to 0x0FFFFFFF), including error handling for invalid length and truncated streams.
  - **Roundtrip**: Validates byte-identical serialization for Format 0, 1, and 2, including edge cases like sysex split packets, note-on velocity 0, meta events, and unknown chunks.
  - **Sequence**: Tests timing calculations for PPQ (tempo changes) and SMPTE (including drop-frame 29.97 fps).
  - **Builder**: Tests `make_note` and `append_absolute` for building valid tracks from scratch.
  - **Errors**: Exhaustively tests the `MidiError` codes (`not_midi`, `truncated`, `missing_status`, `too_many_events`, `input_too_large`, etc.).
  - **Property**: Uses Catch2 generators to run random valid event lists, ensuring `write(read(bytes)) == bytes`.
- `tests/text_test.cpp` & `tests/text_properties_test.cpp`:
  - Validates `normalize` functionality: trimming whitespace, case-folding, and rejecting blank input. Uses property tests to verify idempotence and output formatting.