# Project record

## Objective

Read and write Standard MIDI Files byte for byte, preserving running status and unknown chunks, and merge tracks into a tempo-mapped event timeline in microseconds.

## Scope

- Parsing Standard MIDI Files (SMF) 1.0 (RP-001) into a lossless event model.
- Serializing the model to byte-identical MIDI data.
- Sequencing events into a microsecond timeline using tempo maps (PPQ or SMPTE).
- Enforcing configurable resource limits during read.

## Out-of-scope

- Real-time MIDI I/O or playback.
- General MIDI instrument names or specific synthesis logic.
- RIFF RMID wrappers.
- MIDI 2.0 clip files.

## Effort metrics

| Metric | Value |
|---|---|
| Tokens (input / output) | 4223209 / 52923 |
| Hours (human) | pending measurement |
| Cost (USD) | 1.2421 |

## Factory run u948 (measured from API usage)

| Date (UTC) | Phase | Model | Batch | Input | Output | Cache write | Cache read | Cost (USD) |
|---|---|---|---|---|---|---|---|---|
| 2026-11-17 | generation | google/gemini-4-argon | yes | 7112 | 19934 | 0 | 0 | 0.4271 |
| 2026-11-17 | fix-1 | google/gemini-3.8-flash | no | 97340 | 13155 | 0 | 1082003 | 0.2035 |
| 2026-11-17 | fix-2 | google/gemini-3.8-flash | no | 144038 | 11482 | 0 | 1098286 | 0.2335 |
| 2026-11-17 | fix-3 | google/gemini-3.8-flash | no | 102330 | 4041 | 0 | 504787 | 0.1298 |
| 2026-11-17 | fix-4 | google/gemini-3.8-flash | no | 90158 | 2003 | 0 | 493748 | 0.1122 |
| 2026-11-17 | audit | google/gemini-3.8-flash | no | 120770 | 755 | 0 | 458977 | 0.1278 |
| 2026-11-17 | docs | google/gemini-3.1-flash-lite | no | 23660 | 1553 | 0 | 0 | 0.0082 |

Total: $1.2421. Prices: vertex line, as measured by its adapter. Providers serve the current version of each model id, so the date tells which version answered; versions, launch stages and price sources per line are in the factory's model register (docs/MODELS.md).
