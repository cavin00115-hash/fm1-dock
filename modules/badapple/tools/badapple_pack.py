#!/usr/bin/env python3
"""Build the BADAPPLE module's pack (tools/build.py --pack): the video and the song in one file.

  python3 modules/badapple/tools/badapple_pack.py VIDEO.bav SONG.mid OUT.bin [--delay-ms 1400]

VIDEO.bav: 80 x 60 frames from bav_tool (modules/badapple/host); SONG.mid: the Bad Apple!! MIDI with
its drums split into one track per kit piece. Tracks map to the synth's instruments by name
(ba_synth.h's order); the delay lines the song up with the video.

Pack: "BAPK" u32 version 1, u32 video offset, u32 video length, u32 music offset, u32 music length,
then the video and the music. Music: u32 count, then per event (sorted by time)
  varint delta_ms, u8 inst | vel_level << 5, u8 note, varint dur_ms
"""
import argparse
import struct

INSTRUMENTS = [  # (instrument index, words in the track name), ba_synth.h's order
    (0, "sub bass"), (1, "synth2"), (2, "synth1"), (3, "vocals"), (4, "guitar"), (5, "arp"),
    (7, "bass2"), (6, "bass"), (8, "perc1"), (9, "perc2"),
    (10, "drum - kick"), (11, "drum - clap"), (12, "hihat closed"), (13, "hihat open"), (14, "crash"),
    (15, "shaker"), (16, "conga"), (17, "slap"),
]


def vlq(b, i):
    v = 0
    while True:
        c = b[i]; i += 1
        v = v << 7 | c & 0x7F
        if c < 0x80:
            return v, i


def read_midi(path):
    b = open(path, "rb").read()
    assert b[:4] == b"MThd", "not a MIDI file"
    div = int.from_bytes(b[12:14], "big")
    i, tracks = 14, []
    while i < len(b):
        ln = int.from_bytes(b[i + 4:i + 8], "big")
        j, end, t, st, ev, name = i + 8, i + 8 + ln, 0, 0, [], ""
        while j < end:
            d, j = vlq(b, j); t += d
            if b[j] & 0x80:
                st = b[j]; j += 1
            if st == 0xFF:
                typ = b[j]; l, j = vlq(b, j + 1); data = b[j:j + l]; j += l
                if typ == 0x03 and not name:
                    name = data.decode("latin1")
                ev.append((t, "meta", typ, data))
            elif st in (0xF0, 0xF7):
                l, j = vlq(b, j); j += l
            else:
                n = 1 if st & 0xF0 in (0xC0, 0xD0) else 2
                ev.append((t, "ch", st, b[j:j + n])); j += n
        tracks.append((name, ev))
        i = end
    return div, tracks


def tick_ms(div, tracks):
    tm = sorted((t, int.from_bytes(d, "big")) for _, ev in tracks for t, k, ty, d in ev if k == "meta" and ty == 0x51)
    tm = tm or [(0, 500000)]

    def f(t):
        ms, lt, lv = 0.0, 0, 500000
        for tt, v in tm:
            if tt > t:
                break
            ms += (tt - lt) * lv / div / 1000; lt, lv = tt, v
        return ms + (t - lt) * lv / div / 1000
    return f


def inst_of(name):
    n = name.lower()
    for idx, key in INSTRUMENTS:
        if key in n:
            return idx
    return None


def varint(v):
    out = bytearray()
    while True:
        c = v & 0x7F; v >>= 7
        out.append(c | (0x80 if v else 0))
        if not v:
            return bytes(out)


def music(mid, delay_ms):
    div, tracks = read_midi(mid)
    ms = tick_ms(div, tracks)
    notes, skipped = [], []
    for name, ev in tracks:
        inst = inst_of(name)
        if inst is None:
            if any(k == "ch" and st & 0xF0 == 0x90 for _, k, st, _ in ev):
                skipped.append(name)
            continue
        on = {}
        for t, k, st, d in ev:
            if k != "ch" or st & 0xF0 not in (0x80, 0x90):
                continue
            key = (st & 15, d[0])
            if st & 0xF0 == 0x90 and d[1] > 0:
                on.setdefault(key, []).append((t, d[1]))
            elif on.get(key):
                t0, vel = on[key].pop(0)
                a, b = ms(t0), ms(t)
                notes.append((round(a) + delay_ms, inst, d[0], vel, max(1, min(65535, round(b - a)))))
    notes.sort()
    out, last = bytearray(struct.pack("<I", len(notes))), 0
    for t, inst, note, vel, dur in notes:
        lvl = max(0, min(7, round((vel - 29) / 14)))
        out += varint(t - last) + bytes([inst | lvl << 5, note]) + varint(dur if inst < 10 else 0)
        last = t
    return bytes(out), len(notes), skipped


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video"); ap.add_argument("midi"); ap.add_argument("out")
    ap.add_argument("--delay-ms", type=int, default=1400)
    a = ap.parse_args()
    video = open(a.video, "rb").read()
    assert video[:4] == b"BAV1", "not a .bav video"
    w, h = struct.unpack_from("<HH", video, 4)
    assert (w, h) == (80, 60), f"the module wants 80 x 60, this is {w} x {h}"
    mus, n, skipped = music(a.midi, a.delay_ms)
    head = 24
    vo = head
    mo = vo + len(video) + (-len(video) % 4)
    blob = struct.pack("<4sIIIII", b"BAPK", 1, vo, len(video), mo, len(mus))
    blob += video + b"\0" * (mo - vo - len(video)) + mus
    open(a.out, "wb").write(blob)
    print(f"{a.out}: {len(blob)} B (video {len(video)} B, music {len(mus)} B, {n} notes)")
    if skipped:
        print("  tracks with notes not used (no instrument for the name):", ", ".join(skipped))


if __name__ == "__main__":
    main()
