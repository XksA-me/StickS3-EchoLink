import datetime as dt
import pathlib
import sys
import tempfile
import wave

sys.path.insert(0, str(pathlib.Path(__file__).parents[1] / "server" / "recorder"))
from protocol import (
    AUDIO,
    HEADER,
    MAGIC,
    NUDGE,
    VERSION,
    VOICE_END,
    VOICE_START,
    VoiceAssembler,
    parse_packet,
    save_recording,
)


def frame(packet_type, sender, session, sequence, total, payload=b""):
    header = HEADER.pack(MAGIC, VERSION, packet_type, 0x4D3A91C7, sender, session,
                         sequence, total, len(payload))
    return header + payload


voice = bytes((index % 255 for index in range(16000 * 8)))
assembler = VoiceAssembler()
now = dt.datetime(2026, 7, 20, tzinfo=dt.timezone.utc)
assembler.feed(frame(VOICE_START, 0xD26E27AC, 42, 0, len(voice)), now)
for sequence, offset in enumerate(range(0, len(voice), 200)):
    assembler.feed(frame(AUDIO, 0xD26E27AC, 42, sequence, len(voice),
                         voice[offset:offset + 200]), now)
completed = assembler.feed(frame(VOICE_END, 0xD26E27AC, 42, 0, len(voice)), now)
assert completed is not None

nudge = parse_packet(frame(NUDGE, 0xD26E27AC, 43, 0, 0, b"\x01"))
assert nudge.packet_type == NUDGE and nudge.payload == b"\x01"

try:
    parse_packet(frame(AUDIO, 0xD26E27AC, 44, 0, 1000, b""))
    raise AssertionError("empty audio payload was accepted")
except ValueError:
    pass

with tempfile.TemporaryDirectory() as directory:
    output = save_recording(completed, directory)
    with wave.open(str(output), "rb") as wav_file:
        assert wav_file.getframerate() == 16000
        assert wav_file.getnframes() == 16000 * 8
    assert (pathlib.Path(directory) / "index.jsonl").is_file()

print("PASS: recorder reassembles and stores an eight-second WAV")
