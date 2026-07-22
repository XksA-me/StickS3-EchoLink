from __future__ import annotations

import dataclasses
import datetime as dt
import json
import pathlib
import struct
import wave

MAGIC = 0xEC71
VERSION = 2
VOICE_START = 1
AUDIO = 2
VOICE_END = 3
NUDGE = 4
AUDIO_CHUNK_BYTES = 200
SAMPLE_RATE = 16000
MAX_AUDIO_BYTES = SAMPLE_RATE * 8
HEADER = struct.Struct("<HBBIIHHIH")


@dataclasses.dataclass(frozen=True)
class Packet:
    packet_type: int
    group_id: int
    sender_id: int
    session_id: int
    sequence: int
    total_bytes: int
    payload: bytes


@dataclasses.dataclass
class Session:
    group_id: int
    sender_id: int
    session_id: int
    total_bytes: int
    started_at: dt.datetime
    audio: bytearray
    seen: set[int]
    received_bytes: int = 0


def parse_packet(frame: bytes) -> Packet:
    if len(frame) < HEADER.size:
        raise ValueError("frame is shorter than the protocol header")
    fields = HEADER.unpack_from(frame)
    magic, version, packet_type, group_id, sender_id, session_id, sequence, total_bytes, payload_bytes = fields
    payload = frame[HEADER.size:]
    if magic != MAGIC or version != VERSION:
        raise ValueError("unsupported EchoLink protocol")
    if payload_bytes != len(payload):
        raise ValueError("payload length mismatch")
    if total_bytes > MAX_AUDIO_BYTES:
        raise ValueError("voice exceeds maximum size")
    if packet_type not in (VOICE_START, AUDIO, VOICE_END, NUDGE):
        raise ValueError("unknown packet type")
    if packet_type in (VOICE_START, VOICE_END) and payload:
        raise ValueError("voice boundary packet contains a payload")
    if packet_type == AUDIO and (not payload or len(payload) > AUDIO_CHUNK_BYTES):
        raise ValueError("invalid audio payload size")
    if packet_type == NUDGE and (total_bytes != 0 or len(payload) != 1):
        raise ValueError("invalid nudge payload")
    return Packet(packet_type, group_id, sender_id, session_id, sequence, total_bytes, payload)


class VoiceAssembler:
    def __init__(self) -> None:
        self.sessions: dict[tuple[int, int], Session] = {}

    def feed(self, frame: bytes, now: dt.datetime | None = None) -> Session | None:
        packet = parse_packet(frame)
        timestamp = now or dt.datetime.now(dt.timezone.utc)
        key = (packet.sender_id, packet.session_id)
        if packet.packet_type == VOICE_START:
            if packet.total_bytes == 0:
                return None
            self.sessions[key] = Session(
                group_id=packet.group_id,
                sender_id=packet.sender_id,
                session_id=packet.session_id,
                total_bytes=packet.total_bytes,
                started_at=timestamp,
                audio=bytearray(packet.total_bytes),
                seen=set(),
            )
            return None

        session = self.sessions.get(key)
        if session is None or session.group_id != packet.group_id:
            return None
        if packet.packet_type == AUDIO:
            offset = packet.sequence * AUDIO_CHUNK_BYTES
            if packet.sequence in session.seen or offset >= session.total_bytes:
                return None
            copy_bytes = min(len(packet.payload), session.total_bytes - offset)
            session.audio[offset:offset + copy_bytes] = packet.payload[:copy_bytes]
            session.seen.add(packet.sequence)
            session.received_bytes += copy_bytes
            return None
        if packet.packet_type == VOICE_END:
            self.sessions.pop(key, None)
            return session if session.received_bytes == session.total_bytes else None
        return None

    def discard_stale(self, now: dt.datetime | None = None, max_age_seconds: int = 30) -> None:
        timestamp = now or dt.datetime.now(dt.timezone.utc)
        stale = [key for key, session in self.sessions.items()
                 if (timestamp - session.started_at).total_seconds() > max_age_seconds]
        for key in stale:
            self.sessions.pop(key, None)


def save_recording(session: Session, recordings_dir: str | pathlib.Path) -> pathlib.Path:
    root = pathlib.Path(recordings_dir)
    directory = root / session.started_at.strftime("%Y/%m/%d")
    directory.mkdir(parents=True, exist_ok=True)
    stem = f"{session.started_at.strftime('%H%M%S_%f')}_{session.sender_id:08x}_{session.session_id:04x}"
    wav_path = directory / f"{stem}.wav"
    unsigned_pcm = bytes(sample ^ 0x80 for sample in session.audio)
    with wave.open(str(wav_path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(1)
        wav_file.setframerate(SAMPLE_RATE)
        wav_file.writeframes(unsigned_pcm)

    metadata = {
        "file": str(wav_path.relative_to(root)),
        "timestamp": session.started_at.isoformat(),
        "group_id": f"{session.group_id:08x}",
        "sender_id": f"{session.sender_id:08x}",
        "session_id": session.session_id,
        "duration_seconds": session.total_bytes / SAMPLE_RATE,
        "bytes": session.total_bytes,
    }
    with (root / "index.jsonl").open("a", encoding="utf-8") as index:
        index.write(json.dumps(metadata, ensure_ascii=True) + "\n")
    return wav_path
