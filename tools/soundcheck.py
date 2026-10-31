#!/usr/bin/env python3
"""Check the harness recording of tests/sound.replay: middle C from BASIC's SOUND."""
import array
import sys
import wave

w = wave.open(sys.argv[1])
rate = w.getframerate()
left = array.array("h", w.readframes(w.getnframes()))[0::2]
# The half second after the attack, skipping the noise of the first buffers at boot
step = rate // 2
pos = max(range(rate, len(left) - step, step), key=lambda p: max(left[p:p + step]))
part = left[pos + step:pos + step * 2]
mean = sum(part) / len(part)
crossings = sum(a < mean <= b for a, b in zip(part, part[1:]))
hz = crossings * rate / len(part)
assert rate == 20833, f"sample rate {rate}"
assert 250 < hz < 272, f"tone at {hz:.0f}Hz, middle C is 262Hz"
print(f"ok sound {hz:.0f}Hz")
