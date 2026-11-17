/// @file
/// @brief Standard MIDI File (SMF 1.0) parser, writer, and microsecond sequencer.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @brief The public API of the midi-sequencer library.
namespace trysac::midi_sequencer {

/// @brief Error thrown on invalid MIDI data, encoding violations, or exceeded limits.
class MidiError : public std::invalid_argument {
 public:
  /// @brief Constructs a MidiError with a machine-readable code, message, and byte offset.
  /// @param code Stable machine-readable error code.
  /// @param message Human-readable description of the error.
  /// @param offset Byte offset in the input where the error occurred (0 if not applicable).
  MidiError(std::string code, const std::string& message, std::size_t offset = 0);

  /// @brief Returns the machine-readable error code.
  /// @return One of: "not_midi", "truncated", "invalid_vlq", "missing_status",
  ///         "invalid_format", "input_too_large", "too_many_events", "invalid_event",
  ///         "missing_end_of_track".
  [[nodiscard]] std::string_view code() const noexcept;

  /// @brief Returns the byte offset in the input stream where the error was detected.
  /// @return Byte offset (0 when raised outside byte stream parsing).
  [[nodiscard]] std::size_t offset() const noexcept;

 private:
  std::string code_;
  std::size_t offset_;
};

/// @brief Configurable safety limits enforced when reading untrusted MIDI bytes.
struct ReadLimits {
  /// @brief Maximum total input size in bytes (default 16 MiB).
  std::size_t max_input_bytes = 16'777'216;
  /// @brief Maximum number of MTrk chunks allowed (default 65,535).
  std::size_t max_tracks = 65'535;
  /// @brief Maximum number of events allowed in a single track (default 1,000,000).
  std::size_t max_events_per_track = 1'000'000;
  /// @brief Maximum payload size of a single meta or sysex event in bytes (default 1 MiB).
  std::size_t max_event_bytes = 1'048'576;
};

/// @brief A single MIDI track event (channel message, sysex, or meta event).
struct Event {
  /// @brief Delta time in ticks from the previous event in the same track.
  std::uint32_t delta = 0;
  /// @brief Status byte (0x80..0xEF for channel messages, 0xF0/0xF7 for sysex, 0xFF for meta).
  std::uint8_t status = 0;
  /// @brief Payload bytes (channel parameter bytes, or payload after length for meta/sysex).
  std::vector<std::uint8_t> data;
  /// @brief True if the status byte was omitted in the binary stream via running status.
  bool running_status = false;
  /// @brief Meta event type byte (0x00..0x7F) when status == 0xFF; 0 otherwise.
  std::uint8_t meta_type = 0;

  /// @brief Equality comparison for exact structural and encoding equality.
  /// @param other The event to compare against.
  /// @return True if equal, false otherwise.
  [[nodiscard]] bool operator==(const Event& other) const noexcept = default;
};

/// @brief An unrecognized (non-MThd, non-MTrk) chunk preserved for lossless round-tripping.
struct Chunk {
  /// @brief Four-character ASCII chunk identifier.
  std::array<char, 4> id{};
  /// @brief Raw bytes of the chunk payload.
  std::vector<std::uint8_t> payload;

  /// @brief Equality comparison.
  /// @param other The chunk to compare against.
  /// @return True if equal, false otherwise.
  [[nodiscard]] bool operator==(const Chunk& other) const noexcept = default;
};

/// @brief In-memory representation of a Standard MIDI File 1.0.
struct MidiFile {
  /// @brief SMF format: 0 (single track), 1 (simultaneous tracks), or 2 (independent tracks).
  std::uint16_t format = 0;
  /// @brief Time division: ticks per quarter note (top bit 0) or packed SMPTE (top bit 1).
  std::uint16_t division = 480;
  /// @brief Ordered list of MTrk tracks, each containing its sequence of events.
  std::vector<std::vector<Event>> tracks;
  /// @brief Unknown chunks paired with their index among all chunks after MThd (0-based).
  std::vector<std::pair<std::size_t, Chunk>> other_chunks;

  /// @brief Equality comparison.
  /// @param other The file to compare against.
  /// @return True if equal, false otherwise.
  [[nodiscard]] bool operator==(const MidiFile& other) const noexcept = default;
};

/// @brief An event positioned on an absolute tick and microsecond timeline.
struct TimedEvent {
  /// @brief Cumulative tick position from the start of the track.
  std::uint64_t tick = 0;
  /// @brief Elapsed time from the start of the timeline in microseconds.
  std::int64_t microseconds = 0;
  /// @brief Zero-based index of the MTrk track this event belongs to.
  std::size_t track = 0;
  /// @brief The original event.
  Event event{};

  /// @brief Equality comparison.
  /// @param other The timed event to compare against.
  /// @return True if equal, false otherwise.
  [[nodiscard]] bool operator==(const TimedEvent& other) const noexcept = default;
};

/// @brief Encodes a 28-bit unsigned integer as a Standard MIDI File variable-length quantity.
/// @param value Value to encode (must be <= 0x0FFFFFFF).
/// @return 1 to 4 bytes representing the variable-length quantity.
/// @throws MidiError with code "invalid_vlq" if value > 0x0FFFFFFF.
[[nodiscard]] std::vector<std::uint8_t> encode_vlq(std::uint32_t value);

/// @brief Decodes a Standard MIDI File variable-length quantity from a byte span.
/// @param bytes Input byte buffer.
/// @param offset In/out cursor index; advanced past the decoded VLQ on success.
/// @return Decoded 28-bit unsigned integer.
/// @throws MidiError with code "truncated" if bytes run out before the final byte,
///         or "invalid_vlq" if more than 4 bytes have the continuation bit set.
[[nodiscard]] std::uint32_t decode_vlq(std::span<const std::uint8_t> bytes, std::size_t& offset);

/// @brief Parses a Standard MIDI File from a byte span.
/// @param bytes Raw bytes of the MIDI file.
/// @param limits Safety limits for input size, track count, events per track, and event size.
/// @return Parsed MidiFile preserving running status and unknown chunks.
/// @throws MidiError on malformed input or exceeded limits.
[[nodiscard]] MidiFile read_midi(std::span<const std::uint8_t> bytes, const ReadLimits& limits = {});

/// @brief Serializes a MidiFile into Standard MIDI File 1.0 bytes.
/// @param file The MidiFile model to serialize.
/// @return Raw bytes of the Standard MIDI File.
/// @throws MidiError if the model holds invalid format, events, or VLQ values.
[[nodiscard]] std::vector<std::uint8_t> write_midi(const MidiFile& file);

/// @brief Merges tracks of a MidiFile into a chronological timeline with microsecond positions.
/// @param file The MidiFile to sequence.
/// @return Events ordered by absolute tick, then by track index, then by file order.
/// @throws MidiError with code "invalid_format" if division defines a zero tick rate.
[[nodiscard]] std::vector<TimedEvent> sequence(const MidiFile& file);

/// @brief Creates a note-on and note-off pair tagged with absolute tick positions.
/// @param channel MIDI channel (0..15).
/// @param key MIDI note number (0..127).
/// @param velocity Note-on velocity (0..127).
/// @param start Absolute start tick.
/// @param length Duration in ticks (note-off is placed at start + length with velocity 64).
/// @return Two (absolute_tick, Event) pairs ready for append_absolute.
/// @throws MidiError with code "invalid_event" if channel > 15, key > 127, or velocity > 127.
[[nodiscard]] std::vector<std::pair<std::uint32_t, Event>> make_note(std::uint8_t channel, std::uint8_t key,
                                                                     std::uint8_t velocity, std::uint32_t start,
                                                                     std::uint32_t length);

/// @brief Stably sorts absolute-positioned events, converts times to delta ticks, appends them
///        to `track`, and appends an End-of-Track (FF 2F 00) meta event.
/// @param track Target event vector to populate or extend.
/// @param absolute Pairs of (absolute_tick, Event) to sort and convert.
/// @throws MidiError with code "invalid_vlq" if any computed delta exceeds 0x0FFFFFFF.
void append_absolute(std::vector<Event>& track, std::vector<std::pair<std::uint32_t, Event>> absolute);

}  // namespace trysac::midi_sequencer
