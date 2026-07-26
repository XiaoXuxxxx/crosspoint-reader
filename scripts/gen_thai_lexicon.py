#!/usr/bin/env python3
"""Generate a flash-resident Thai word lexicon header from words_th.txt.

Reads a PyThaiNLP word list (CC0-1.0), filters to Thai-only no-space entries,
decomposes Sara Am (U+0E33 -> U+0E4D + U+0E32 with tone mark reordering) to
match the NFC composition applied by utf8ComposeNfc() at layout time, filters
to the most frequent words using the Thai National Corpus frequency list
(tnc_freq.txt, also CC0-1.0), sorts by UTF-8 byte order, and emits a
front-coded binary blob as a constexpr header.

The string pool is block-restart front coded (LevelDB-SSTable style): sorted
Thai words share long byte prefixes, so each word stores only the prefix length
shared with its predecessor plus its suffix. Every RESTART_INTERVAL-th word is
stored in full as a "restart point", giving a sparse offset table for binary
search. This roughly halves the blob versus a flat null-terminated pool with a
full per-entry offset table. See ThaiLexicon.h for the matching decode logic.

Dual-mode: runs as a PlatformIO pre-build script (auto-detects env) or as a
standalone CLI tool with --input/--output arguments.
"""

from __future__ import annotations

import argparse
import pathlib
import struct

# Words this many UTF-8 bytes or longer are dropped: the C++ lookup uses a
# fixed stack buffer (ThaiLexicon.h THAI_LEXICON_MAX_ENTRY_BYTES) and
# reconstructs entries into it, so a longer entry would overflow it. Real Thai
# words are far shorter (~42 chars max, 3 bytes each); this only guards corrupt input.
MAX_WORD_BYTES = 128

# Every RESTART_INTERVAL-th sorted entry is stored uncompressed as a binary-
# search anchor. Smaller = faster lookup, larger = better compression. 16 keeps
# the in-block linear scan tiny while capturing most of the prefix sharing.
# MUST match the implicit block boundaries decoded by ThaiLexicon.h.
RESTART_INTERVAL = 16


# --- Sara Am decomposition (must match Utf8.cpp:80-131, utf8DecomposeThaiSaraAm) ---


def decompose_sara_am(codepoints: list[int]) -> list[int]:
    """Decompose U+0E33 (Sara Am) -> U+0E4D (Nikhahit) + U+0E32 (Sara Aa).

    If tone marks (U+0E48-0E4B) precede, reorders:
      <C, tone, 0E33> -> <C, 0E4D, tone, 0E32>

    This matches the C++ utf8DecomposeThaiSaraAm() behavior in Utf8.cpp:80-131.
    """
    result: list[int] = []
    for cp in codepoints:
        if cp == 0x0E33:
            tones: list[int] = []
            while result and 0x0E48 <= result[-1] <= 0x0E4B:
                tones.insert(0, result.pop())
            result.append(0x0E4D)  # Nikhahit (combining mark, above)
            result.extend(tones)  # Re-emit tone marks after Nikhahit
            result.append(0x0E32)  # Sara Aa (base vowel, advances cursor)
        else:
            result.append(cp)
    return result


def contains_thai(codepoints: list[int]) -> bool:
    return any(0x0E01 <= cp <= 0x0E5B for cp in codepoints)


def has_whitespace(text: str) -> bool:
    return any(c.isspace() for c in text)


# --- Binary format ---


def build_blob(entries: list[str]) -> bytes:
    """Build the flash-resident front-coded binary blob from sorted entries.

    Layout (little-endian):
      [4 bytes: uint32 restart_count]
      [4 * restart_count bytes: uint32 absolute offset of each block's first
         record, from blob start]
      [blocks ...]

    Each block holds up to RESTART_INTERVAL records:
      - first record (the restart point): [u8 key_len][key_len bytes]
      - each later record:                [u8 shared_len][u8 suffix_len][suffix]
        where the full word = previous_word[:shared_len] + suffix.
    A block ends where the next block's offset begins (the last block ends at
    the blob end). entries MUST be sorted by UTF-8 byte order and deduplicated.
    """
    encoded = [e.encode("utf-8") for e in entries]
    restart_count = (len(encoded) + RESTART_INTERVAL - 1) // RESTART_INTERVAL
    header_size = 4 + 4 * restart_count

    offsets: list[int] = []
    blocks = bytearray()
    prev = b""
    for i, enc in enumerate(encoded):
        # Guarded upstream, but never let a corrupt long word corrupt the blob.
        assert len(enc) < MAX_WORD_BYTES, f"entry too long: {len(enc)} bytes"
        if i % RESTART_INTERVAL == 0:
            offsets.append(header_size + len(blocks))
            blocks.append(len(enc))
            blocks.extend(enc)
        else:
            shared = 0
            limit = min(len(prev), len(enc))
            while shared < limit and prev[shared] == enc[shared]:
                shared += 1
            suffix = enc[shared:]
            blocks.append(shared)
            blocks.append(len(suffix))
            blocks.extend(suffix)
        prev = enc

    blob = bytearray()
    blob.extend(struct.pack("<I", restart_count))
    for off in offsets:
        blob.extend(struct.pack("<I", off))
    blob.extend(blocks)
    return bytes(blob)


def format_bytes(data: bytes, per_line: int = 16) -> str:
    lines = []
    for i in range(0, len(data), per_line):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i : i + per_line])
        lines.append(f"    {chunk},")
    if not lines:
        lines.append("    0x00,")
    return "\n".join(lines)


def write_header(path: pathlib.Path, blob: bytes, entry_count: int) -> None:
    restart_count = struct.unpack_from("<I", blob, 0)[0]

    path.parent.mkdir(parents=True, exist_ok=True)

    content = f"""#pragma once

#include <cstddef>
#include <cstdint>

#include "Epub/thai/ThaiLexicon.h"

// Auto-generated by gen_thai_lexicon.py. Do not edit manually.
// Source: words_th.txt (CC0-1.0, PyThaiNLP project)
// {entry_count} entries in {restart_count} front-coded blocks, {len(blob)} bytes total.
alignas(4) constexpr uint8_t thai_lexicon_data[] = {{
{format_bytes(blob)}
}};

constexpr ThaiLexicon thai_lexicon = {{
    thai_lexicon_data,
    sizeof(thai_lexicon_data),
    {restart_count}u,
}};
"""
    path.write_text(content)


def load_frequencies(freq_path: pathlib.Path | None) -> dict[str, int]:
    """Load word frequencies from tnc_freq.txt (tab-separated: word\\tfreq)."""
    if freq_path is None or not freq_path.exists():
        return {}
    freq_map: dict[str, int] = {}
    for line in freq_path.read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) == 2:
            try:
                freq_map[parts[0]] = int(parts[1])
            except ValueError:
                continue
    return freq_map


def generate(
    input_path: pathlib.Path,
    output_path: pathlib.Path,
    freq_path: pathlib.Path | None = None,
    max_entries: int = 12000,
) -> None:
    freq_map = load_frequencies(freq_path)

    # Build (word, frequency) pairs for all Thai-only no-space entries.
    pairs: list[tuple[str, int]] = []
    for line in input_path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or has_whitespace(line):
            continue
        codepoints = [ord(c) for c in line]
        if not contains_thai(codepoints):
            continue
        decomposed = decompose_sara_am(codepoints)
        word = "".join(chr(cp) for cp in decomposed)
        # Drop words that would overflow the C++ lookup/reconstruction buffer.
        if len(word.encode("utf-8")) >= MAX_WORD_BYTES:
            continue
        freq = freq_map.get(line, 0)
        pairs.append((word, freq))

    # Sort by frequency descending; ties keep alphabetical order (stable sort).
    pairs.sort(key=lambda p: (-p[1], p[0]))

    # Take the most frequent entries.
    selected = pairs[:max_entries]

    # Sort selected entries by UTF-8 byte order for binary search.
    entries = [p[0] for p in selected]
    entries.sort()

    # Deduplicate (sorted array allows binary search).
    unique: list[str] = []
    for e in entries:
        if not unique or unique[-1] != e:
            unique.append(e)

    blob = build_blob(unique)
    write_header(output_path, blob, len(unique))
    print(f"wrote {output_path} ({len(unique)} entries, {len(blob)} bytes)")


def main_cli() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="Path to words_th.txt")
    parser.add_argument("--output", required=True, help="Path to output .h file")
    parser.add_argument("--freq", default=None, help="Path to tnc_freq.txt (frequency filter)")
    parser.add_argument("--max-entries", type=int, default=12000, help="Max entries to keep")
    args = parser.parse_args()
    generate(
        pathlib.Path(args.input),
        pathlib.Path(args.output),
        pathlib.Path(args.freq) if args.freq else None,
        args.max_entries,
    )


if __name__ == "__main__":
    main_cli()
else:
    # PlatformIO pre-build entry point.
    # PlatformIO exec()'s pre-scripts without defining __file__, so use
    # relative paths (PlatformIO runs from the project root).
    try:
        Import("env")
        generate(
            pathlib.Path("data/lexicon/words_th.txt"),
            pathlib.Path("lib/Epub/Epub/thai/generated/thaiLexicon.generated.h"),
            pathlib.Path("data/lexicon/tnc_freq.txt"),
            12000,
        )
    except NameError:
        pass
