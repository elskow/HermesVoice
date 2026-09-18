package stt

import (
	"context"
	"fmt"

	"hermes-voice/internal/config"
)

type Transcript struct {
	Text       string
	Confidence float64
	LatencyMs  int
}

type Provider interface {
	Transcribe(ctx context.Context, pcm []byte, sampleRate, channels int) (Transcript, error)
}

const MaxPCMBytes = 30 * 16000 * 2

func New(cfg config.Config) (Provider, error) {
	switch cfg.STTProvider {
	case "deepgram":
		return newDeepgram(cfg), nil
	case "deepgram-stream":
		return newDeepgramStream(cfg), nil
	case "whisper":
		return newWhisper(cfg), nil
	case "stub":
		return newStub(), nil
	default:
		return nil, fmt.Errorf("unknown STT_PROVIDER %q", cfg.STTProvider)
	}
}
