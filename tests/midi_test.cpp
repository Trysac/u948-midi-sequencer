#include "trysac/midi_sequencer/midi.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace midi = trysac::midi_sequencer;

namespace {

void CheckMidiError(const auto& fn, std::string_view expected_code) {
  bool thrown = false;
  try {
    fn();
  } catch (const midi::MidiError& err) {
    thrown = true;
    CHECK(err.code() == expected_code);
    CHECK(!std::string_view(err.what()).empty());
  }
  CHECK(thrown);
}

}  // namespace

TEST_CASE("VLQ encoding and decoding matches SMF 1.0 specification table", "[vlq]") {
  struct Case {
    std::uint32_t value;
    std::vector<std::uint8_t> bytes;
  };

  const std::vector<Case> table = {
      {.value = 0x00000000U, .bytes = {0x00U}},
      {.value = 0x00000040U, .bytes = {0x40U}},
      {.value = 0x0000007FU, .bytes = {0x7FU}},
      {.value = 0x00000080U, .bytes = {0x81U, 0x00U}},
      {.value = 0x00002000U, .bytes = {0xC0U, 0x00U}},
      {.value = 0x00003FFFU, .bytes = {0xFFU, 0x7FU}},
      {.value = 0x00004000U, .bytes = {0x81U, 0x80U, 0x00U}},
      {.value = 0x0FFFFFFFU, .bytes = {0xFFU, 0xFFU, 0xFFU, 0x7FU}},
  };

  for (const auto& tc : table) {
    const auto encoded = midi::encode_vlq(tc.value);
    CHECK(encoded == tc.bytes);

    std::size_t offset = 0;
    const std::uint32_t decoded = midi::decode_vlq(encoded, offset);
    CHECK(decoded == tc.value);
    CHECK(offset == encoded.size());
  }

  CheckMidiError([] { static_cast<void>(midi::encode_vlq(0x10000000U)); }, "invalid_vlq");

  // 5-byte VLQ throws invalid_vlq
  const std::vector<std::uint8_t> five_bytes = {0x81U, 0x80U, 0x80U, 0x80U, 0x00U};
  CheckMidiError(
      [&] {
        std::size_t off = 0;
        static_cast<void>(midi::decode_vlq(five_bytes, off));
      },
      "invalid_vlq");

  // Truncated VLQ throws truncated
  const std::vector<std::uint8_t> truncated_vlq = {0x81U, 0x80U};
  CheckMidiError(
      [&] {
        std::size_t off = 0;
        static_cast<void>(midi::decode_vlq(truncated_vlq, off));
      },
      "truncated");
}

TEST_CASE("Byte-identical round-trip across formats, running status, sysex, meta, and unknown chunks", "[roundtrip]") {
  SECTION("Format 0 with running status, note-on velocity 0, channel pressure, meta, and sysex split") {
    const std::vector<std::uint8_t> bytes = {
        // MThd: format 0, 1 track, division 480 (0x01E0)
        'M',
        'T',
        'h',
        'd',
        0x00,
        0x00,
        0x00,
        0x06,
        0x00,
        0x00,
        0x00,
        0x01,
        0x01,
        0xE0,
        // Unknown chunk "XFIH" before MTrk
        'X',
        'F',
        'I',
        'H',
        0x00,
        0x00,
        0x00,
        0x04,
        0xDE,
        0xAD,
        0xBE,
        0xEF,
        // MTrk length 41 (0x29)
        'M',
        'T',
        'r',
        'k',
        0x00,
        0x00,
        0x00,
        0x29,
        // delta 0, meta event with zero length (FF 01 00)
        0x00,
        0xFF,
        0x01,
        0x00,
        // delta 0, sysex packet 1: F0 03 7E 7F 09
        0x00,
        0xF0,
        0x03,
        0x7E,
        0x7F,
        0x09,
        // delta 10, sysex continuation packet 2: F7 02 01 F7
        0x0A,
        0xF7,
        0x02,
        0x01,
        0xF7,
        // delta 0, Note On ch 0: 90 3C 64
        0x00,
        0x90,
        0x3C,
        0x64,
        // delta 120, running status Note On with velocity 0: 3C 00
        0x78,
        0x3C,
        0x00,
        // delta 0, running status Note On: 40 50
        0x00,
        0x40,
        0x50,
        // delta 60, explicit Note Off: 80 40 40
        0x3C,
        0x80,
        0x40,
        0x40,
        // delta 0, Channel Pressure (D0, 1 data byte)
        0x00,
        0xD0,
        0x30,
        // delta 16, running status Channel Pressure (1 data byte)
        0x10,
        0x45,
        // delta 0, Program Change (C0, 1 data byte)
        0x00,
        0xC0,
        0x05,
        // delta 0, End of Track: FF 2F 00
        0x00,
        0xFF,
        0x2F,
        0x00,
    };

    const midi::MidiFile parsed = midi::read_midi(bytes);
    CHECK(parsed.format == 0);
    CHECK(parsed.division == 480);
    REQUIRE(parsed.other_chunks.size() == 1);
    CHECK(parsed.other_chunks.at(0).first == 0);
    CHECK(parsed.other_chunks.at(0).second.id == std::array<char, 4>{'X', 'F', 'I', 'H'});
    REQUIRE(parsed.tracks.size() == 1);
    CHECK(parsed.tracks.at(0).at(4).running_status == true);
    CHECK(parsed.tracks.at(0).at(8).running_status == true);

    const auto rewritten = midi::write_midi(parsed);
    CHECK(rewritten == bytes);
  }

  SECTION("Format 1 with 3 tracks and trailing unknown chunk") {
    const std::vector<std::uint8_t> bytes = {
        // MThd: format 1, 3 tracks, division 96
        'M',
        'T',
        'h',
        'd',
        0x00,
        0x00,
        0x00,
        0x06,
        0x00,
        0x01,
        0x00,
        0x03,
        0x00,
        0x60,
        // Track 0 (tempo track)
        'M',
        'T',
        'r',
        'k',
        0x00,
        0x00,
        0x00,
        0x0B,
        0x00,
        0xFF,
        0x51,
        0x03,
        0x07,
        0xA1,
        0x20,
        0x00,
        0xFF,
        0x2F,
        0x00,
        // Track 1
        'M',
        'T',
        'r',
        'k',
        0x00,
        0x00,
        0x00,
        0x0B,
        0x00,
        0x90,
        0x3C,
        0x40,
        0x60,
        0x3C,
        0x00,
        0x00,
        0xFF,
        0x2F,
        0x00,
        // Track 2
        'M',
        'T',
        'r',
        'k',
        0x00,
        0x00,
        0x00,
        0x08,
        0x00,
        0x91,
        0x43,
        0x50,
        0x00,
        0xFF,
        0x2F,
        0x00,
        // Unknown chunk "XFIH" at chunk index 3
        'X',
        'F',
        'I',
        'H',
        0x00,
        0x00,
        0x00,
        0x02,
        0x12,
        0x34,
    };

    const midi::MidiFile parsed = midi::read_midi(bytes);
    CHECK(parsed.format == 1);
    CHECK(parsed.tracks.size() == 3);
    CHECK(midi::write_midi(parsed) == bytes);
  }

  SECTION("Format 2 with 2 tracks and missing end-of-track accepted and preserved") {
    const std::vector<std::uint8_t> bytes = {
        'M',  'T', 'h', 'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x02, 0x00, 0x02, 0x01,
        0xE0, 'M', 'T', 'r', 'k',  0x00, 0x00, 0x00, 0x04, 0x00, 0x90, 0x3C, 0x64,
        'M',  'T', 'r', 'k', 0x00, 0x00, 0x00, 0x04, 0x00, 0x80, 0x3C, 0x40,
    };

    const midi::MidiFile parsed = midi::read_midi(bytes);
    CHECK(parsed.format == 2);
    CHECK(midi::write_midi(parsed) == bytes);
  }

  SECTION("Hand-edited model with invalid running_status flag emits explicit status byte") {
    midi::MidiFile file{};
    file.format = 0;
    file.division = 480;
    const midi::Event ev1{.delta = 0, .status = 0x90, .data = {60, 100}, .running_status = true, .meta_type = 0};
    const midi::Event meta{.delta = 0, .status = 0xFF, .data = {}, .running_status = false, .meta_type = 0x01};
    const midi::Event ev2{.delta = 10, .status = 0x90, .data = {60, 0}, .running_status = true, .meta_type = 0};
    file.tracks.push_back({ev1, meta, ev2});

    const auto raw = midi::write_midi(file);
    const auto reread = midi::read_midi(raw);
    REQUIRE(reread.tracks.size() == 1);
    REQUIRE(reread.tracks.at(0).size() == 3);
    CHECK(reread.tracks.at(0).at(0).running_status == false);
    CHECK(reread.tracks.at(0).at(2).running_status == false);
  }
}

TEST_CASE("Timing and sequence() ordering across PPQ, tempo changes, format 1, format 2, and SMPTE", "[sequence]") {
  SECTION("Default tempo 500000 and tempo change FF 51 03 03 D0 90 (250000) at tick 480") {
    midi::MidiFile file{};
    file.format = 0;
    file.division = 480;
    file.tracks.push_back({
        midi::Event{.delta = 960, .status = 0x90, .data = {60, 100}, .running_status = false, .meta_type = 0},
    });

    const auto seq_default = midi::sequence(file);
    REQUIRE(seq_default.size() == 1);
    CHECK(seq_default.at(0).tick == 960);
    CHECK(seq_default.at(0).microseconds == 1'000'000);

    // Now insert tempo event FF 51 03 03 D0 90 (250000) at tick 480
    midi::MidiFile file_tempo{};
    file_tempo.format = 0;
    file_tempo.division = 480;
    file_tempo.tracks.push_back({
        midi::Event{
            .delta = 480,
            .status = 0xFF,
            .data = {0x03, 0xD0, 0x90},
            .running_status = false,
            .meta_type = 0x51,
        },
        midi::Event{.delta = 480, .status = 0x90, .data = {60, 100}, .running_status = false, .meta_type = 0},
    });

    const auto seq_tempo = midi::sequence(file_tempo);
    REQUIRE(seq_tempo.size() == 2);
    CHECK(seq_tempo.at(0).tick == 480);
    CHECK(seq_tempo.at(0).microseconds == 500'000);
    CHECK(seq_tempo.at(1).tick == 960);
    CHECK(seq_tempo.at(1).microseconds == 750'000);
  }

  SECTION("Format 1 applies track 0 tempo to all tracks and orders by tick, track index, file order") {
    midi::MidiFile file{};
    file.format = 1;
    file.division = 480;
    // Track 0: tempo 250000 at tick 480
    file.tracks.push_back({
        midi::Event{
            .delta = 480,
            .status = 0xFF,
            .data = {0x03, 0xD0, 0x90},
            .running_status = false,
            .meta_type = 0x51,
        },
    });
    // Track 1: two events at tick 480, one at tick 960
    file.tracks.push_back({
        midi::Event{.delta = 480, .status = 0x90, .data = {60, 100}, .running_status = false, .meta_type = 0},
        midi::Event{.delta = 0, .status = 0x90, .data = {64, 100}, .running_status = true, .meta_type = 0},
        midi::Event{.delta = 480, .status = 0x80, .data = {60, 64}, .running_status = false, .meta_type = 0},
    });

    const auto seq = midi::sequence(file);
    REQUIRE(seq.size() == 4);
    CHECK(seq.at(0).track == 0);
    CHECK(seq.at(0).tick == 480);
    CHECK(seq.at(1).track == 1);
    CHECK(seq.at(1).tick == 480);
    CHECK(seq.at(1).event.data.at(0) == 60);
    CHECK(seq.at(2).track == 1);
    CHECK(seq.at(2).tick == 480);
    CHECK(seq.at(2).event.data.at(0) == 64);
    CHECK(seq.at(3).track == 1);
    CHECK(seq.at(3).tick == 960);
    CHECK(seq.at(3).microseconds == 750'000);
  }

  SECTION("Format 2 sequences tracks with independent tempo maps") {
    midi::MidiFile file{};
    file.format = 2;
    file.division = 480;
    // Track 0 changes tempo to 250000 at tick 0
    file.tracks.push_back({
        midi::Event{.delta = 0, .status = 0xFF, .data = {0x03, 0xD0, 0x90}, .running_status = false, .meta_type = 0x51},
        midi::Event{.delta = 480, .status = 0x90, .data = {60, 100}, .running_status = false, .meta_type = 0},
    });
    // Track 1 stays at default 500000
    file.tracks.push_back({
        midi::Event{.delta = 480, .status = 0x91, .data = {62, 100}, .running_status = false, .meta_type = 0},
    });

    const auto seq = midi::sequence(file);
    REQUIRE(seq.size() == 3);
    CHECK(seq.at(1).track == 0);
    CHECK(seq.at(1).microseconds == 250'000);
    CHECK(seq.at(2).track == 1);
    CHECK(seq.at(2).microseconds == 500'000);
  }

  SECTION("SMPTE division (25 fps and 29.97 drop-frame)") {
    midi::MidiFile file{};
    file.format = 0;
    // -25 fps (0xE7), 40 ticks/frame (0x28) => 1000 ticks/sec
    file.division = 0xE728U;
    file.tracks.push_back({
        midi::Event{.delta = 500, .status = 0x90, .data = {60, 100}, .running_status = false, .meta_type = 0},
    });
    const auto seq = midi::sequence(file);
    REQUIRE(seq.size() == 1);
    CHECK(seq.at(0).microseconds == 500'000);

    // -29 (29.97 fps = 0xE3), 100 ticks/frame (0x64)
    file.division = 0xE364U;
    file.tracks.at(0).at(0).delta = 3000;
    const auto seq29 = midi::sequence(file);
    REQUIRE(seq29.size() == 1);
    CHECK(seq29.at(0).microseconds == 1'001'000);

    // Invalid SMPTE fps
    file.division = 0x8100U;
    CheckMidiError([&] { static_cast<void>(midi::sequence(file)); }, "invalid_format");
  }
}

TEST_CASE("make_note and append_absolute build a valid track from scratch", "[builder]") {
  midi::MidiFile file{};
  file.format = 0;
  file.division = 480;

  std::vector<midi::Event> track;
  auto note_events = midi::make_note(0, 60, 100, 0, 480);
  midi::append_absolute(track, std::move(note_events));
  file.tracks.push_back(std::move(track));

  const auto bytes = midi::write_midi(file);
  const auto parsed = midi::read_midi(bytes);

  REQUIRE(parsed.tracks.size() == 1);
  const auto& trk = parsed.tracks.at(0);
  REQUIRE(trk.size() == 3);

  // Note-on 90 3C 64 at tick 0
  CHECK(trk.at(0).delta == 0);
  CHECK(trk.at(0).status == 0x90U);
  CHECK(trk.at(0).data == std::vector<std::uint8_t>{0x3CU, 0x64U});

  // Note-off 80 3C 40 at tick 480
  CHECK(trk.at(1).delta == 480);
  CHECK(trk.at(1).status == 0x80U);
  CHECK(trk.at(1).data == std::vector<std::uint8_t>{0x3CU, 0x40U});

  // Ends with FF 2F 00
  CHECK(trk.at(2).delta == 0);
  CHECK(trk.at(2).status == 0xFFU);
  CHECK(trk.at(2).meta_type == 0x2FU);
  CHECK(trk.at(2).data.empty());

  // Appending a second batch replaces trailing EOT and maintains monotonic ticks
  auto second_note = midi::make_note(0, 64, 90, 960, 240);
  midi::append_absolute(file.tracks.at(0), std::move(second_note));
  REQUIRE(file.tracks.at(0).size() == 5);
  CHECK(file.tracks.at(0).at(2).delta == 480);
  CHECK(file.tracks.at(0).at(4).meta_type == 0x2FU);

  // Out-of-range parameters throw invalid_event
  CheckMidiError([] { static_cast<void>(midi::make_note(16, 60, 100, 0, 480)); }, "invalid_event");
  CheckMidiError([] { static_cast<void>(midi::make_note(0, 128, 100, 0, 480)); }, "invalid_event");
  CheckMidiError([] { static_cast<void>(midi::make_note(0, 60, 128, 0, 480)); }, "invalid_event");
}

TEST_CASE("Error codes and ReadLimits enforcement", "[errors]") {
  SECTION("not_midi when header does not start with MThd") {
    const std::vector<std::uint8_t> bad = {'R', 'I', 'F', 'F', 0, 0, 0, 6, 0, 0, 0, 1, 1, 0xE0};
    CheckMidiError([&] { static_cast<void>(midi::read_midi(bad)); }, "not_midi");
  }

  SECTION("truncated on short header or chunk length past end") {
    const std::vector<std::uint8_t> short_hdr = {'M', 'T', 'h', 'd', 0, 0};
    CheckMidiError([&] { static_cast<void>(midi::read_midi(short_hdr)); }, "truncated");

    const std::vector<std::uint8_t> past_end = {
        'M',  'T', 'h', 'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01, 0x01,
        0xE0, 'M', 'T', 'r', 'k',  0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x2F, 0x00,
    };
    CheckMidiError([&] { static_cast<void>(midi::read_midi(past_end)); }, "truncated");
  }

  SECTION("missing_status when data byte appears with no active status") {
    const std::vector<std::uint8_t> no_status = {
        'M',  'T', 'h', 'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01, 0x01,
        0xE0, 'M', 'T', 'r', 'k',  0x00, 0x00, 0x00, 0x03, 0x00, 0x3C, 0x64,
    };
    CheckMidiError([&] { static_cast<void>(midi::read_midi(no_status)); }, "missing_status");
  }

  SECTION("invalid_format when format 0 has 2 tracks or format > 2") {
    const std::vector<std::uint8_t> fmt0_two_tracks = {
        'M', 'T', 'h', 'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x02, 0x01, 0xE0,
    };
    CheckMidiError([&] { static_cast<void>(midi::read_midi(fmt0_two_tracks)); }, "invalid_format");

    const std::vector<std::uint8_t> fmt3 = {
        'M', 'T', 'h', 'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x03, 0x00, 0x01, 0x01, 0xE0,
    };
    CheckMidiError([&] { static_cast<void>(midi::read_midi(fmt3)); }, "invalid_format");
  }

  SECTION("input_too_large and too_many_events limits") {
    const midi::ReadLimits default_limits{};
    CHECK(default_limits.max_input_bytes == 16'777'216U);
    CHECK(default_limits.max_tracks == 65'535U);
    CHECK(default_limits.max_events_per_track == 1'000'000U);
    CHECK(default_limits.max_event_bytes == 1'048'576U);

    const std::vector<std::uint8_t> valid = {
        'M', 'T', 'h', 'd',  0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01, 0x01, 0xE0, 'M',
        'T', 'r', 'k', 0x00, 0x00, 0x00, 0x08, 0x00, 0x90, 0x3C, 0x64, 0x00, 0xFF, 0x2F, 0x00,
    };

    midi::ReadLimits small_input{};
    small_input.max_input_bytes = 10;
    CheckMidiError([&] { static_cast<void>(midi::read_midi(valid, small_input)); }, "input_too_large");

    midi::ReadLimits one_event{};
    one_event.max_events_per_track = 1;
    CheckMidiError([&] { static_cast<void>(midi::read_midi(valid, one_event)); }, "too_many_events");
  }

  SECTION("invalid_event on bad channel data byte or system common byte") {
    const std::vector<std::uint8_t> bad_data_msb = {
        'M',  'T', 'h', 'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01, 0x01,
        0xE0, 'M', 'T', 'r', 'k',  0x00, 0x00, 0x00, 0x04, 0x00, 0x90, 0x80, 0x64,
    };
    CheckMidiError([&] { static_cast<void>(midi::read_midi(bad_data_msb)); }, "invalid_event");

    const std::vector<std::uint8_t> bad_sys_common = {
        'M',  'T',  'h', 'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01,
        0x01, 0xE0, 'M', 'T', 'r',  'k',  0x00, 0x00, 0x00, 0x02, 0x00, 0xF2,
    };
    CheckMidiError([&] { static_cast<void>(midi::read_midi(bad_sys_common)); }, "invalid_event");
  }

  SECTION("write_midi validation errors") {
    midi::MidiFile bad_file{};
    bad_file.format = 3;
    CheckMidiError([&] { static_cast<void>(midi::write_midi(bad_file)); }, "invalid_format");

    bad_file.format = 0;
    bad_file.division = 0;
    CheckMidiError([&] { static_cast<void>(midi::write_midi(bad_file)); }, "invalid_format");

    bad_file.division = 480;
    bad_file.tracks = {
        {
            midi::Event{
                .delta = 0,
                .status = 0x90,
                .data = {60},
                .running_status = false,
                .meta_type = 0,
            },
        },
    };  // Only 1 data byte for Note On
    CheckMidiError([&] { static_cast<void>(midi::write_midi(bad_file)); }, "invalid_event");
  }
}

TEST_CASE("Property: random valid event lists written and read back are equal", "[property]") {
  const int seed_iter = GENERATE(take(25, random(1, 100000)));
  midi::MidiFile file{};
  file.format = 1;
  file.division = 480;

  std::vector<midi::Event> track;
  std::uint8_t prev_status = 0;
  for (int i = 0; i < 12; ++i) {
    const auto v = static_cast<std::uint32_t>((seed_iter * (i + 1) * 2654435761U) & 0x0FFFFFFFU);
    midi::Event ev{};
    ev.delta = v % 16384U;
    const int kind = (seed_iter + i) % 4;
    if (kind == 0) {
      ev.status = static_cast<std::uint8_t>(0x90U | (v & 0x0FU));
      ev.data = {static_cast<std::uint8_t>(v & 0x7FU), static_cast<std::uint8_t>((v >> 7U) & 0x7FU)};
      ev.running_status = (prev_status == ev.status);
      prev_status = ev.status;
    } else if (kind == 1) {
      ev.status = static_cast<std::uint8_t>(0xC0U | (v & 0x0FU));
      ev.data = {static_cast<std::uint8_t>(v & 0x7FU)};
      ev.running_status = (prev_status == ev.status);
      prev_status = ev.status;
    } else if (kind == 2) {
      ev.status = 0xFFU;
      ev.meta_type = 0x01U;
      ev.data = {'t', 'e', 's', 't'};
      ev.running_status = false;
      prev_status = 0;
    } else {
      ev.status = 0xF0U;
      ev.data = {0x7EU, 0x7FU, 0xF7U};
      ev.running_status = false;
      prev_status = 0;
    }
    track.push_back(std::move(ev));
  }
  track.push_back(midi::Event{
      .delta = 0,
      .status = 0xFFU,
      .data = {},
      .running_status = false,
      .meta_type = 0x2FU,
  });
  file.tracks.push_back(std::move(track));

  const auto bytes = midi::write_midi(file);
  const auto decoded = midi::read_midi(bytes);
  CHECK(decoded == file);
  CHECK(midi::write_midi(decoded) == bytes);
}
