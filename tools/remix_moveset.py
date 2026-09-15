"""Decode Smash Remix moveset binaries into timed commands and hitbox windows."""
import struct

MULTIWORD = {3: 5, 4: 5, 7: 2, 12: 2, 13: 2, 31: 4, 34: 2, 36: 2, 38: 4, 39: 4, 46: 2}

# nFTMotionEvent sound commands (word >> 26).
SOUND_OPCODES = {14, 15, 17, 18, 19, 20}


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def parse_throw_desc(data):
    """FTThrowHitDesc: status, damage, angle, scale, weight, base, element."""
    if len(data) < 28:
        return None
    status, damage, angle, growth, weight, base, element = struct.unpack(">7i", data[:28])
    if status < 0 or damage < 0 or damage > 999:
        return None
    return [status, damage, angle, growth, weight, base, element]


def decode_moveset(data):
    """Decode a self-contained binary into timed commands and hitbox windows.

    Pointer commands are skipped (the importer inlines THROW_DATA / GO_TO files
    separately). Missing END is treated as the end of a looping movement script.
    """
    if len(data) % 4:
        raise ValueError("unaligned moveset")
    words = struct.unpack(">" + str(len(data) // 4) + "I", data)
    frame = 0
    pc = 0
    active = {}
    hits = []
    events = []
    loops = []
    budget = 10000

    def close(aid):
        if aid in active:
            begin, values = active.pop(aid)
            if frame > begin:
                hits.append([begin, frame, *values])

    def finish():
        for aid in list(active):
            close(aid)
        return hits, events

    while pc < len(words):
        budget -= 1
        if budget <= 0:
            raise ValueError("moveset instruction budget exceeded")
        word = words[pc]
        op = word >> 26
        count = MULTIWORD.get(op, 1)
        if pc + count > len(words):
            raise ValueError(f"truncated opcode {op} at {pc * 4}")
        args = words[pc:pc + count]
        pc += count
        events.append([frame, op, count, *args, *([0] * (5 - count))])
        if op == 0:
            return finish()
        if op == 1:
            frame += word & 0x3ffffff
        elif op == 2:
            frame = max(frame, word & 0x3ffffff)
        elif op in (3, 4):
            aid = (word >> 23) & 7
            close(aid)
            active[aid] = (frame, [aid, (word >> 20) & 7, signed((word >> 13) & 127, 7),
                (word >> 5) & 255, args[1] >> 16, signed(args[1] & 65535, 16),
                signed(args[2] >> 16, 16), signed(args[2] & 65535, 16),
                (args[3] >> 22) & 1023, (args[3] >> 12) & 1023, (args[3] >> 2) & 1023,
                (args[4] >> 7) & 1023, word & 15, args[3] & 3, signed(args[4] >> 24, 8),
                (args[4] >> 17) & 15, (args[4] >> 21) & 7, int(op == 4)])
        elif op == 5:
            close(word & 0x3ffffff)
        elif op == 6:
            for aid in list(active):
                close(aid)
        elif op in (7, 8, 9, 10, 11):
            aid = (word & 0x3ffffff) if op == 11 else (word >> 23) & 7
            if aid in active:
                values = active[aid][1].copy()
                close(aid)
                if op == 7:
                    values[5:8] = [signed((word >> 7) & 65535, 16), signed(args[1] >> 16, 16), signed(args[1] & 65535, 16)]
                elif op == 8:
                    values[3] = (word >> 15) & 255
                elif op == 9:
                    values[4] = (word >> 7) & 65535
                elif op == 10:
                    values[16] = (word >> 20) & 7
                active[aid] = (frame, values)
        elif op == 32:
            iterations = word & 0x3ffffff
            if not 0 < iterations <= 1000:
                raise ValueError("invalid loop count")
            loops.append([pc, iterations])
        elif op == 33:
            if not loops:
                raise ValueError("unmatched loop end")
            loops[-1][1] -= 1
            if loops[-1][1]:
                pc = loops[-1][0]
            else:
                loops.pop()
        elif op == 36:
            # GO_TO in assembled ROM; raw bins use this as a loop terminator.
            return finish()
    return finish()
