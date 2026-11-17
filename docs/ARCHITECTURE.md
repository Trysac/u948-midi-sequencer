# Architecture

`trysac::midi_sequencer` is a zero-dependency C++20 library implementing Standard MIDI Files 1.0 (MIDI Manufacturers Association, RP-001).

## Components

- **VLQ Codec (`encode_vlq`, `decode_vlq`)**: Encodes and decodes 28-bit variable-length quantities (up to 4 bytes, maximum `0x0FFFFFFF`).
- **Lossless Event Model (`Event`, `Chunk`, `MidiFile`)**:
  - `Event::running_status` records whether the status byte was omitted in the stream.
  - `write_midi` emits a running-status omission only when `running_status == true` and the previous event in the track was a channel message with the identical status byte; meta (`0xFF`) and sysex (`0xF0`/`0xF7`) events cancel running status.
  - Unknown chunks are stored in `MidiFile::other_chunks` with their 0-based position among chunks following `MThd`, ensuring byte-identical round-tripping. Extra padding bytes after an End-of-Track event inside an `MTrk` are not stored separately; only non-`MTrk` chunks round-trip through `other_chunks`.
- **Sequencer (`sequence`)**:
  - Merges events across tracks ordered by `(tick, track_index, file_order)`.
  - Computes microsecond timestamps in `std::int64_t` per tempo segment (`ticks * tempo / division`), applying Track 0 / global tempo changes across tracks in Format 0 and 1, and per-track tempo maps in Format 2.
