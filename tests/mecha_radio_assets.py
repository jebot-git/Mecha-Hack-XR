"""Check the published NPC radio bank independently of the mixer's legacy-clip fallback."""
import hashlib
import json
import sys
import wave
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
bank = Path(sys.argv[1]) / "fx/audio/radio"
dialogue = json.loads((repo / "tools/mecha_assets/npc_dialogue.json").read_text())
factions = ("primacy", "terran", "dominion")
situations = ("friendly_contact", "friendly_support", "friendly_confirm",
              "enemy_contact", "enemy_attack", "enemy_distress")
assert set(dialogue) == set(factions)
assert {p.name for p in bank.glob("*.wav")} == {f"{i}.wav" for i in range(72)}
voices, scripts = set(), set()
for f, faction in enumerate(factions):
    assert set(dialogue[faction]) == set(situations)
    for s, event in enumerate(situations):
        lines = dialogue[faction][event]
        assert len(lines) == 4
        for take, line in enumerate(lines):
            clip = 36*(take//2) + 12*f + 2*s + take%2
            assert line.strip() and line not in scripts, (clip, line)
            scripts.add(line)
            with wave.open(str(bank / f"{clip}.wav"), "rb") as wav:
                assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 24000), clip
                frames = wav.getnframes()
                # The scheduler reserves seven seconds per transmission, including its HRTF tail.
                assert 0 < frames / wav.getframerate() + 512 / 48000 < 7, (clip, frames)
                pcm = wav.readframes(frames)
                assert len(pcm) == 2*frames and any(pcm), clip
                key = hashlib.sha256(pcm).digest()
                assert key not in voices, f"Repeated recording at slot {clip}"
                voices.add(key)
print(f"{len(voices)} distinct published NPC recordings mapped to {len(scripts)} distinct lines.")
