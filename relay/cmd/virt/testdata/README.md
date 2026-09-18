# Fixture clips (all 16 kHz mono S16LE raw PCM, matching the ESP32 mic path)

- `voice.pcm` (87,552 B, 2.7 s): **real speech**, Indonesian male
  (`id-ID-ArdiNeural` at -30% rate, free Edge TTS): "Tolong deploy ke staging sekarang".
  Peak ~20k (silence gate is 300). Use for real-provider runs: Deepgram
  returns the sentence above; Hermes replies for real. Regenerate with
  `edge-tts --voice id-ID-ArdiNeural` + ffmpeg to s16le if it rots.
- `speech.pcm` (48,754 B): synthetic ramp. Passes gates, meaningless to
  STT. Plumbing only (stub runs, chunk-boundary checks).
- `empty.pcm` (3,200 zero bytes): tap-gate boundary size, all silence.
  Expects the `silence` path.
