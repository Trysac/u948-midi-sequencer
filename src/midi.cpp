#include "trysac/midi_sequencer/midi.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trysac::midi_sequencer {

namespace {

constexpr std::uint32_t kMaxVlq = 0x0FFFFFFFU;
constexpr std::int64_t kDefaultTempo = 500'000;

bool is_channel_status(std::uint8_t status) noexcept { return status >= 0x80U && status <= 0xEFU; }

std::size_t channel_data_length(std::uint8_t status) noexcept {
  const std::uint8_t high = status & 0xF0U;
  if (high == 0xC0U || high == 0xD0U) {
    return 1;
  }
  return 2;
}

std::uint16_t read_u16_be(std::span<const std::uint8_t> bytes, std::size_t pos) noexcept {
  const auto sub = bytes.subspan(pos, 2);
  return static_cast<std::uint16_t>((static_cast<std::uint32_t>(sub.front()) << 8U) |
                                    static_cast<std::uint32_t>(sub.back()));
}

std::uint32_t read_u32_be(std::span<const std::uint8_t> bytes, std::size_t pos) noexcept {
  const auto sub = bytes.subspan(pos, 4);
  auto it = sub.begin();
  const auto b0 = static_cast<std::uint32_t>(*it++);
  const auto b1 = static_cast<std::uint32_t>(*it++);
  const auto b2 = static_cast<std::uint32_t>(*it++);
  const auto b3 = static_cast<std::uint32_t>(*it);
  return (b0 << 24U) | (b1 << 16U) | (b2 << 8U) | b3;
}

void append_u16_be(std::vector<std::uint8_t>& out, std::uint16_t val) {
  const auto val32 = static_cast<std::uint32_t>(val);
  out.push_back(static_cast<std::uint8_t>((val32 >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(val32 & 0xFFU));
}

void append_u32_be(std::vector<std::uint8_t>& out, std::uint32_t val) {
  out.push_back(static_cast<std::uint8_t>((val >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((val >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((val >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(val & 0xFFU));
}

std::vector<Event> parse_track(std::span<const std::uint8_t> bytes, std::size_t track_start, std::size_t track_end,
                               const ReadLimits& limits) {
  std::vector<Event> events;
  std::size_t pos = track_start;
  std::uint8_t running_status = 0;

  while (pos < track_end) {
    if (events.size() >= limits.max_events_per_track) {
      throw MidiError("too_many_events", "track exceeds max_events_per_track", pos);
    }
    const std::uint32_t delta = decode_vlq(bytes.subspan(0, track_end), pos);
    if (pos >= track_end) {
      throw MidiError("truncated", "unexpected end of track after delta time", pos);
    }

    const std::size_t status_pos = pos;
    const std::uint8_t first_byte = bytes.subspan(pos, 1).front();
    Event ev{};
    ev.delta = delta;

    if (first_byte == 0xFFU) {
      running_status = 0;
      ++pos;
      if (pos >= track_end) {
        throw MidiError("truncated", "truncated meta event type", pos);
      }
      const std::uint8_t meta_type = bytes.subspan(pos++, 1).front();
      if (meta_type > 0x7FU) {
        throw MidiError("invalid_event", "meta event type must be < 128", pos - 1);
      }
      const std::uint32_t len = decode_vlq(bytes.subspan(0, track_end), pos);
      if (len > limits.max_event_bytes) {
        throw MidiError("input_too_large", "meta event payload exceeds max_event_bytes", pos);
      }
      if (len > track_end - pos) {
        throw MidiError("truncated", "truncated meta event payload", pos);
      }
      ev.status = 0xFFU;
      ev.meta_type = meta_type;
      ev.running_status = false;
      const auto payload_span = bytes.subspan(pos, len);
      ev.data.assign(payload_span.begin(), payload_span.end());
      pos += len;
    } else if (first_byte == 0xF0U || first_byte == 0xF7U) {
      running_status = 0;
      ++pos;
      const std::uint32_t len = decode_vlq(bytes.subspan(0, track_end), pos);
      if (len > limits.max_event_bytes) {
        throw MidiError("input_too_large", "sysex payload exceeds max_event_bytes", pos);
      }
      if (len > track_end - pos) {
        throw MidiError("truncated", "truncated sysex event payload", pos);
      }
      ev.status = first_byte;
      ev.running_status = false;
      const auto payload_span = bytes.subspan(pos, len);
      ev.data.assign(payload_span.begin(), payload_span.end());
      pos += len;
    } else if (is_channel_status(first_byte)) {
      running_status = first_byte;
      ++pos;
      const std::size_t need = channel_data_length(running_status);
      if (need > track_end - pos) {
        throw MidiError("truncated", "truncated channel event data", pos);
      }
      ev.status = running_status;
      ev.running_status = false;
      ev.data.reserve(need);
      const auto channel_data = bytes.subspan(pos, need);
      for (std::size_t i = 0; i < need; ++i) {
        const std::uint8_t d = channel_data.subspan(i, 1).front();
        if ((d & 0x80U) != 0U) {
          throw MidiError("invalid_event", "channel data byte has MSB set", pos + i);
        }
        ev.data.push_back(d);
      }
      pos += need;
    } else if (first_byte < 0x80U) {
      if (running_status == 0U) {
        throw MidiError("missing_status", "data byte encountered without active running status", status_pos);
      }
      const std::size_t need = channel_data_length(running_status);
      if (need > track_end - pos) {
        throw MidiError("truncated", "truncated running-status channel event data", pos);
      }
      ev.status = running_status;
      ev.running_status = true;
      ev.data.reserve(need);
      const auto channel_data = bytes.subspan(pos, need);
      for (std::size_t i = 0; i < need; ++i) {
        const std::uint8_t d = channel_data.subspan(i, 1).front();
        if ((d & 0x80U) != 0U) {
          throw MidiError("invalid_event", "channel data byte has MSB set", pos + i);
        }
        ev.data.push_back(d);
      }
      pos += need;
    } else {
      throw MidiError("invalid_event", "unsupported system common or real-time status in SMF", status_pos);
    }

    events.push_back(std::move(ev));
  }

  return events;
}

std::vector<std::uint8_t> serialize_track(const std::vector<Event>& track) {
  std::vector<std::uint8_t> buf;
  std::uint8_t active_running_status = 0;

  for (const auto& ev : track) {
    const auto delta_bytes = encode_vlq(ev.delta);
    buf.insert(buf.end(), delta_bytes.begin(), delta_bytes.end());

    if (ev.status == 0xFFU) {
      if (ev.meta_type > 0x7FU) {
        throw MidiError("invalid_event", "meta event type must be < 128");
      }
      active_running_status = 0;
      buf.push_back(0xFFU);
      buf.push_back(ev.meta_type);
      const auto len_bytes = encode_vlq(static_cast<std::uint32_t>(ev.data.size()));
      buf.insert(buf.end(), len_bytes.begin(), len_bytes.end());
      buf.insert(buf.end(), ev.data.begin(), ev.data.end());
    } else if (ev.status == 0xF0U || ev.status == 0xF7U) {
      active_running_status = 0;
      buf.push_back(ev.status);
      const auto len_bytes = encode_vlq(static_cast<std::uint32_t>(ev.data.size()));
      buf.insert(buf.end(), len_bytes.begin(), len_bytes.end());
      buf.insert(buf.end(), ev.data.begin(), ev.data.end());
    } else if (is_channel_status(ev.status)) {
      const std::size_t expected = channel_data_length(ev.status);
      if (ev.data.size() != expected) {
        throw MidiError("invalid_event", "channel message has wrong number of data bytes");
      }
      for (const std::uint8_t b : ev.data) {
        if ((b & 0x80U) != 0U) {
          throw MidiError("invalid_event", "channel data byte has MSB set");
        }
      }
      if (!ev.running_status || active_running_status != ev.status) {
        buf.push_back(ev.status);
      }
      active_running_status = ev.status;
      buf.insert(buf.end(), ev.data.begin(), ev.data.end());
    } else {
      throw MidiError("invalid_event", "invalid event status byte");
    }
  }

  return buf;
}

struct TempoChange {
  std::uint64_t tick;
  std::int64_t tempo;
};

bool is_tempo_event(const Event& ev, std::int64_t& out_tempo) noexcept {
  if (ev.status == 0xFFU && ev.meta_type == 0x51U && ev.data.size() == 3U) {
    const auto b0 = static_cast<std::uint32_t>(ev.data.at(0));
    const auto b1 = static_cast<std::uint32_t>(ev.data.at(1));
    const auto b2 = static_cast<std::uint32_t>(ev.data.at(2));
    out_tempo = static_cast<std::int64_t>((b0 << 16U) | (b1 << 8U) | b2);
    return true;
  }
  return false;
}

std::int64_t compute_ppq_microseconds(std::uint64_t target_tick, const std::vector<TempoChange>& tempo_map,
                                      std::int64_t division) noexcept {
  std::int64_t total_us = 0;
  std::uint64_t prev_tick = 0;
  std::int64_t current_tempo = kDefaultTempo;

  for (const auto& tc : tempo_map) {
    if (tc.tick >= target_tick) {
      break;
    }
    if (tc.tick > prev_tick) {
      const auto seg_ticks = static_cast<std::int64_t>(tc.tick - prev_tick);
      total_us += (seg_ticks * current_tempo) / division;
      prev_tick = tc.tick;
    }
    current_tempo = tc.tempo;
  }

  if (target_tick > prev_tick) {
    const auto seg_ticks = static_cast<std::int64_t>(target_tick - prev_tick);
    total_us += (seg_ticks * current_tempo) / division;
  }

  return total_us;
}

std::int64_t compute_smpte_microseconds(std::uint64_t target_tick, std::uint16_t division) {
  const auto div32 = static_cast<std::uint32_t>(division);
  const auto upper_u8 = static_cast<std::uint8_t>((div32 >> 8U) & 0xFFU);
  const auto upper_byte = static_cast<std::int8_t>(upper_u8);
  const int fps = -static_cast<int>(upper_byte);
  const auto ticks_per_frame = static_cast<std::int64_t>(division & 0xFFU);
  if ((fps != 24 && fps != 25 && fps != 29 && fps != 30) || ticks_per_frame <= 0) {
    throw MidiError("invalid_format", "invalid SMPTE division parameters");
  }
  const auto ticks = static_cast<std::int64_t>(target_tick);
  if (fps == 29) {
    // 29.97 fps = 30000 / 1001 frames per second
    return (ticks * 1'000'000LL * 1001LL) / (30000LL * ticks_per_frame);
  }
  return (ticks * 1'000'000LL) / (static_cast<std::int64_t>(fps) * ticks_per_frame);
}

}  // namespace

MidiError::MidiError(std::string code, const std::string& message, std::size_t offset)
    : std::invalid_argument(message), code_(std::move(code)), offset_(offset) {}

std::string_view MidiError::code() const noexcept { return code_; }

std::size_t MidiError::offset() const noexcept { return offset_; }

std::vector<std::uint8_t> encode_vlq(std::uint32_t value) {
  if (value > kMaxVlq) {
    throw MidiError("invalid_vlq", "VLQ value exceeds maximum 0x0FFFFFFF");
  }
  std::vector<std::uint8_t> buf;
  buf.push_back(static_cast<std::uint8_t>(value & 0x7FU));
  value >>= 7U;
  while (value > 0U) {
    buf.push_back(static_cast<std::uint8_t>((value & 0x7FU) | 0x80U));
    value >>= 7U;
  }
  std::ranges::reverse(buf);
  return buf;
}

std::uint32_t decode_vlq(std::span<const std::uint8_t> bytes, std::size_t& offset) {
  std::uint32_t value = 0;
  std::size_t count = 0;
  const std::size_t start_offset = offset;

  while (true) {
    if (offset >= bytes.size()) {
      throw MidiError("truncated", "unexpected end of input while decoding VLQ", offset);
    }
    const std::uint8_t b = bytes.subspan(offset++, 1).front();
    ++count;
    if (count > 4U) {
      throw MidiError("invalid_vlq", "VLQ exceeds 4 bytes", start_offset);
    }
    value = (value << 7U) | static_cast<std::uint32_t>(b & 0x7FU);
    if ((b & 0x80U) == 0U) {
      break;
    }
    if (count == 4U) {
      throw MidiError("invalid_vlq", "VLQ exceeds 4 bytes", start_offset);
    }
  }
  return value;
}

MidiFile read_midi(std::span<const std::uint8_t> bytes, const ReadLimits& limits) {
  if (bytes.size() > limits.max_input_bytes) {
    throw MidiError("input_too_large", "input size exceeds max_input_bytes", bytes.size());
  }
  if (bytes.size() < 4U) {
    throw MidiError("not_midi", "missing MThd header magic", 0);
  }
  const std::array<std::uint8_t, 4> mthd_magic{'M', 'T', 'h', 'd'};
  if (!std::ranges::equal(bytes.subspan(0, 4), mthd_magic)) {
    throw MidiError("not_midi", "missing MThd header magic", 0);
  }
  if (bytes.size() < 14U) {
    throw MidiError("truncated", "truncated MThd header chunk", bytes.size());
  }

  const std::uint32_t header_len = read_u32_be(bytes, 4);
  if (header_len < 6U) {
    throw MidiError("invalid_format", "MThd chunk length must be at least 6", 4);
  }
  if (header_len > bytes.size() - 8U) {
    throw MidiError("truncated", "MThd chunk length extends past end of input", 4);
  }

  const std::uint16_t format = read_u16_be(bytes, 8);
  const std::uint16_t ntrks = read_u16_be(bytes, 10);
  const std::uint16_t division = read_u16_be(bytes, 12);

  if (format > 2U) {
    throw MidiError("invalid_format", "unsupported MIDI format (must be 0, 1, or 2)", 8);
  }
  if (format == 0U && ntrks > 1U) {
    throw MidiError("invalid_format", "format 0 cannot have more than 1 track", 10);
  }
  if (ntrks > limits.max_tracks) {
    throw MidiError("invalid_format", "track count exceeds max_tracks", 10);
  }
  if (division == 0U) {
    throw MidiError("invalid_format", "division cannot be zero", 12);
  }

  MidiFile file{};
  file.format = format;
  file.division = division;

  std::size_t pos = 8U + static_cast<std::size_t>(header_len);
  std::size_t chunk_index = 0;

  while (pos < bytes.size()) {
    if (bytes.size() - pos < 8U) {
      throw MidiError("truncated", "truncated chunk header", pos);
    }
    const auto id_sub = bytes.subspan(pos, 4);
    auto id_it = id_sub.begin();
    const std::array<char, 4> chunk_id{
        static_cast<char>(*id_it++),
        static_cast<char>(*id_it++),
        static_cast<char>(*id_it++),
        static_cast<char>(*id_it),
    };
    const std::uint32_t chunk_len = read_u32_be(bytes, pos + 4);
    pos += 8U;
    if (chunk_len > bytes.size() - pos) {
      throw MidiError("truncated", "chunk length extends past end of input", pos - 4U);
    }

    if (chunk_id == std::array<char, 4>{'M', 'T', 'r', 'k'}) {
      if (file.tracks.size() >= limits.max_tracks) {
        throw MidiError("invalid_format", "number of MTrk chunks exceeds max_tracks", pos - 8U);
      }
      file.tracks.push_back(parse_track(bytes, pos, pos + chunk_len, limits));
    } else {
      Chunk unknown{};
      unknown.id = chunk_id;
      const auto payload_span = bytes.subspan(pos, chunk_len);
      unknown.payload.assign(payload_span.begin(), payload_span.end());
      file.other_chunks.emplace_back(chunk_index, std::move(unknown));
    }

    pos += chunk_len;
    ++chunk_index;
  }

  if (file.tracks.size() != ntrks) {
    throw MidiError("truncated", "MTrk count does not match MThd ntrks", pos);
  }

  return file;
}

std::vector<std::uint8_t> write_midi(const MidiFile& file) {
  if (file.format > 2U) {
    throw MidiError("invalid_format", "unsupported MIDI format (must be 0, 1, or 2)");
  }
  if (file.format == 0U && file.tracks.size() > 1U) {
    throw MidiError("invalid_format", "format 0 cannot have more than 1 track");
  }
  if (file.tracks.size() > 0xFFFFU) {
    throw MidiError("invalid_format", "too many tracks for 16-bit ntrks field");
  }
  if (file.division == 0U) {
    throw MidiError("invalid_format", "division cannot be zero");
  }

  std::vector<std::uint8_t> out;
  out.reserve(64);

  // Write MThd
  out.insert(out.end(), {'M', 'T', 'h', 'd'});
  append_u32_be(out, 6U);
  append_u16_be(out, file.format);
  append_u16_be(out, static_cast<std::uint16_t>(file.tracks.size()));
  append_u16_be(out, file.division);

  const std::size_t total_chunks = file.tracks.size() + file.other_chunks.size();
  std::size_t track_cursor = 0;

  // Interleave MTrk and unknown chunks according to their recorded chunk index
  std::vector<std::pair<std::size_t, const Chunk*>> sorted_other;
  sorted_other.reserve(file.other_chunks.size());
  for (const auto& entry : file.other_chunks) {
    sorted_other.emplace_back(entry.first, &entry.second);
  }
  std::ranges::stable_sort(sorted_other, [](const auto& a, const auto& b) { return a.first < b.first; });

  std::size_t other_cursor = 0;
  for (std::size_t idx = 0; idx < total_chunks; ++idx) {
    if (other_cursor < sorted_other.size() &&
        (sorted_other.at(other_cursor).first <= idx || track_cursor >= file.tracks.size())) {
      const Chunk* c = sorted_other.at(other_cursor++).second;
      for (const char ch : c->id) {
        out.push_back(static_cast<std::uint8_t>(ch));
      }
      append_u32_be(out, static_cast<std::uint32_t>(c->payload.size()));
      out.insert(out.end(), c->payload.begin(), c->payload.end());
    } else if (track_cursor < file.tracks.size()) {
      const auto payload = serialize_track(file.tracks.at(track_cursor++));
      out.insert(out.end(), {'M', 'T', 'r', 'k'});
      append_u32_be(out, static_cast<std::uint32_t>(payload.size()));
      out.insert(out.end(), payload.begin(), payload.end());
    }
  }

  return out;
}

std::vector<TimedEvent> sequence(const MidiFile& file) {
  if (file.division == 0U) {
    throw MidiError("invalid_format", "division cannot be zero");
  }

  const bool is_smpte = (file.division & 0x8000U) != 0U;
  const auto ppq = static_cast<std::int64_t>(file.division & 0x7FFFU);

  struct IndexedEvent {
    std::uint64_t tick;
    std::size_t track;
    std::size_t order;
    Event event;
  };

  std::vector<IndexedEvent> all_events;
  std::vector<std::vector<TempoChange>> track_tempos(file.tracks.size());
  std::vector<TempoChange> global_tempos;

  for (std::size_t t = 0; t < file.tracks.size(); ++t) {
    std::uint64_t tick = 0;
    const auto& track = file.tracks.at(t);
    for (std::size_t i = 0; i < track.size(); ++i) {
      const auto& ev = track.at(i);
      tick += ev.delta;
      all_events.push_back(IndexedEvent{.tick = tick, .track = t, .order = i, .event = ev});
      std::int64_t tempo = 0;
      if (is_tempo_event(ev, tempo)) {
        track_tempos.at(t).push_back(TempoChange{.tick = tick, .tempo = tempo});
      }
    }
  }

  std::ranges::stable_sort(all_events, [](const IndexedEvent& a, const IndexedEvent& b) {
    if (a.tick != b.tick) {
      return a.tick < b.tick;
    }
    if (a.track != b.track) {
      return a.track < b.track;
    }
    return a.order < b.order;
  });

  if (!is_smpte && file.format != 2U) {
    for (const auto& ie : all_events) {
      std::int64_t tempo = 0;
      if (is_tempo_event(ie.event, tempo)) {
        global_tempos.push_back(TempoChange{.tick = ie.tick, .tempo = tempo});
      }
    }
  }

  std::vector<TimedEvent> result;
  result.reserve(all_events.size());

  for (auto& ie : all_events) {
    std::int64_t us = 0;
    if (is_smpte) {
      us = compute_smpte_microseconds(ie.tick, file.division);
    } else if (file.format == 2U) {
      us = compute_ppq_microseconds(ie.tick, track_tempos.at(ie.track), ppq);
    } else {
      us = compute_ppq_microseconds(ie.tick, global_tempos, ppq);
    }
    result.push_back(TimedEvent{.tick = ie.tick, .microseconds = us, .track = ie.track, .event = std::move(ie.event)});
  }

  return result;
}

std::vector<std::pair<std::uint32_t, Event>> make_note(std::uint8_t channel, std::uint8_t key, std::uint8_t velocity,
                                                       std::uint32_t start, std::uint32_t length) {
  if (channel > 15U || key > 127U || velocity > 127U) {
    throw MidiError("invalid_event", "MIDI channel must be 0..15 and key/velocity 0..127");
  }

  Event note_on{};
  note_on.delta = 0;
  note_on.status = static_cast<std::uint8_t>(0x90U | channel);
  note_on.data = {key, velocity};

  Event note_off{};
  note_off.delta = 0;
  note_off.status = static_cast<std::uint8_t>(0x80U | channel);
  note_off.data = {key, 64U};

  return {
      {start, std::move(note_on)},
      {start + length, std::move(note_off)},
  };
}

void append_absolute(std::vector<Event>& track, std::vector<std::pair<std::uint32_t, Event>> absolute) {
  std::ranges::stable_sort(absolute, [](const auto& a, const auto& b) { return a.first < b.first; });

  // Remove trailing end-of-track if already present so we can append cleanly
  std::uint32_t current_tick = 0;
  if (!track.empty() && track.back().status == 0xFFU && track.back().meta_type == 0x2FU) {
    track.pop_back();
  }
  for (const auto& ev : track) {
    current_tick += ev.delta;
  }

  for (auto& [abs_tick, ev] : absolute) {
    const std::uint32_t delta = (abs_tick >= current_tick) ? (abs_tick - current_tick) : 0U;
    if (delta > kMaxVlq) {
      throw MidiError("invalid_vlq", "computed delta exceeds maximum VLQ");
    }
    ev.delta = delta;
    current_tick = (abs_tick >= current_tick) ? abs_tick : current_tick;
    track.push_back(std::move(ev));
  }

  Event eot{};
  eot.delta = 0;
  eot.status = 0xFFU;
  eot.meta_type = 0x2FU;
  eot.data = {};
  track.push_back(std::move(eot));
}

}  // namespace trysac::midi_sequencer
