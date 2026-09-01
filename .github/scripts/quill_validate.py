#!/usr/bin/env python3
"""
Validates real Atari AdventureWriter/Quill .xex game files against the
format understanding used by engines/glk/quill in this fork (see
quill_types.h / database.cpp for the authoritative C++ version - this is
an independent, stdlib-only re-implementation used purely to cross-check
real-world files in CI, where this fork's sandbox has no direct network
access to game archives).

Never redistributes or retains the input files themselves - only reports
structural/statistical facts (sizes, table offsets, computed hashes, and a
short truncated text sample for a human sanity check) rather than full
game content.
"""
import sys
import glob
import hashlib
import os
import json
import zipfile
import struct

INTERPRETER_MD5 = "b9107db7d9e6d4c4a884e8f7bc47a886"
DATABASE_BASE = 0x1D00
INTERPRETER_BASE = 0x7C0D
INTERPRETER_SIZE = 3599


def read_xex_segments(data):
    segments = []
    pos = 0
    first = True
    while pos + 4 <= len(data):
        word = struct.unpack_from('<H', data, pos)[0]
        pos += 2
        if word == 0xFFFF:
            if pos + 2 > len(data):
                break
            start = struct.unpack_from('<H', data, pos)[0]
            pos += 2
        elif first:
            return segments if segments else None
        else:
            start = word

        if pos + 2 > len(data):
            break
        end = struct.unpack_from('<H', data, pos)[0]
        pos += 2
        if end < start:
            break

        length = end - start + 1
        seg_data = data[pos:pos + length]
        if len(seg_data) != length:
            break

        segments.append((start, end, seg_data))
        pos += length
        first = False

    return segments if segments else None


def decode_string(data, offset):
    out = []
    if offset < 0 or offset >= len(data):
        return '', offset
    while offset < len(data):
        raw = data[offset]
        offset += 1
        dec = raw ^ 0xFF
        if dec == 0:
            break
        if dec == 0x9B:
            out.append('\n')
            continue
        ch = dec & 0x7F
        if 0x20 <= ch <= 0x7E:
            out.append(chr(ch))
    return ''.join(out), offset


def analyse(path, data):
    result = {"file": path, "size": len(data)}

    segs = read_xex_segments(data)
    if not segs:
        result["error"] = "not a valid .xex (no $FFFF-prefixed segment structure found)"
        return result

    result["segments"] = [
        {"start": hex(s), "end": hex(e), "len": len(d)} for s, e, d in segs
    ]

    db = None
    interp_ok = False
    for start, end, d in segs:
        if start == DATABASE_BASE:
            db = d
        if start == INTERPRETER_BASE and len(d) == INTERPRETER_SIZE:
            h = hashlib.md5(d).hexdigest()
            result["interpreter_md5"] = h
            interp_ok = (h == INTERPRETER_MD5)

    result["interpreter_matches_known_adventurewriter"] = interp_ok

    if not db:
        result["error"] = "no database segment found at $1D00"
        return result
    if len(db) < 31:
        result["error"] = "database segment too small for a valid header"
        return result

    hdr = struct.unpack_from('<BBBBBBBBBHHHHHHHHHHH', db, 0)
    (unused0, color1, color2, color4, maxCarry, objCount, locCount, msgCount,
     sysMsgCount, eventAddr, statusAddr, objAddr, locAddr, msgAddr, sysMsgAddr,
     moveAddr, vocabAddr, objLocAddr, endOfDb, unused29) = hdr

    result["header"] = {
        "objectCount": objCount,
        "locationCount": locCount,
        "messageCount": msgCount,
        "systemMessageCount": sysMsgCount,
        "maxCarry": maxCarry,
        "endOfDatabase": hex(endOfDb),
    }

    def text_at(table_addr, idx):
        off = (table_addr - DATABASE_BASE) + idx * 2
        if off < 0 or off + 1 >= len(db):
            return None
        addr = db[off] | (db[off + 1] << 8)
        if addr < DATABASE_BASE:
            return None
        s, _ = decode_string(db, addr - DATABASE_BASE)
        return s

    sample_loc = text_at(locAddr, 1)
    result["sample_location_1_first_60_chars"] = (sample_loc or "")[:60]
    result["sample_location_1_looks_printable"] = bool(sample_loc) and sample_loc.isprintable()

    voc_words = []
    off = vocabAddr - DATABASE_BASE
    while off >= 0 and off + 5 <= len(db):
        raw = db[off:off + 4]
        if 0 in raw:
            break
        w = ''.join(chr(b ^ 0xFF) for b in raw).rstrip(' ')
        voc_words.append(w)
        off += 5
    result["vocabulary_word_count"] = len(voc_words)
    result["vocabulary_sample"] = voc_words[:8]

    return result


def collect_files(roots):
    found = []
    for root in roots:
        for ext in ("*.xex", "*.XEX", "*.Xex"):
            found += [(p, None) for p in glob.glob(os.path.join(root, "**", ext), recursive=True)]
        for ext in ("*.zip", "*.ZIP"):
            for z in glob.glob(os.path.join(root, "**", ext), recursive=True):
                try:
                    with zipfile.ZipFile(z) as zf:
                        for name in zf.namelist():
                            if name.lower().endswith(".xex"):
                                found.append((f"{z}!{name}", zf.read(name)))
                except Exception as e:
                    print(f"  (couldn't open {z}: {e})", file=sys.stderr)
    return found


def main():
    roots = sys.argv[1:] or ["."]
    results = []

    for path, preloaded in collect_files(roots):
        if preloaded is None:
            with open(path, 'rb') as fh:
                data = fh.read()
        else:
            data = preloaded
        results.append(analyse(path, data))

    print(json.dumps(results, indent=2))

    verified = [r for r in results if r.get("interpreter_matches_known_adventurewriter")]
    print(
        f"\n{len(results)} .xex file(s) examined, {len(verified)} confirmed as genuine "
        f"AdventureWriter/Quill Atari databases (interpreter MD5 match).",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
