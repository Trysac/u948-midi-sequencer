# midi-sequencer

Music tools that edit MIDI files often rewrite them differently than they were read (dropping running status, converting note-off to note-on with velocity 0, or dropping unknown chunks), which breaks diff-based workflows and round-trip tests. Timing is also prone to errors during tempo changes. This library provides a zero-dependency C++20 implementation to parse Standard MIDI Files into an event model that preserves every encoding detail, writes them back byte-identical, and merges tracks into a tempo-mapped timeline in microseconds.

## Usage

```cpp
#include "trysac/midi_sequencer/midi.hpp"

namespace midi = trysac::midi_sequencer;

midi::MidiFile file{.format = 0, .division = 480};
std::vector<midi::Event> track;
midi::append_absolute(track, midi::make_note(0, 60, 100, 0, 480));
file.tracks.push_back(std::move(track));
const auto bytes = midi::write_midi(file);
const auto timeline = midi::sequence(midi::read_midi(bytes));
```

## API Summary

- `read_midi(bytes, limits)`: Parses binary data into `MidiFile`, enforcing `ReadLimits` (max_input_bytes 16,777,216, max_tracks 65,535, max_events_per_track 1,000,000, max_event_bytes 1,048,576).
- `write_midi(file)`: Serializes `MidiFile` back to byte-identical binary format.
- `sequence(file)`: Computes a chronological timeline of events in microseconds, handling PPQ or SMPTE timing and tempo changes.
- `encode_vlq` / `decode_vlq`: Codecs for standard 4-byte MIDI variable-length quantities.
- `make_note`: Helper to generate note-on/note-off pairs.
- `append_absolute`: Helper to build tracks from absolute-timed events.
- `MidiError`: Exception class with codes: `not_midi`, `truncated`, `invalid_vlq`, `missing_status`, `invalid_format`, `input_too_large`, `too_many_events`, `invalid_event`.

## Why not existing libraries?

Many MIDI libraries do not support lossless round-tripping of raw MIDI stream details like running status and unknown chunks, and few provide exact microsecond timing with typed error handling and configurable resource limits.

See [docs/SECURITY.md](docs/SECURITY.md) for security notes.