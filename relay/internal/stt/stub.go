package stt

import "context"

type stub struct{}

func newStub() *stub { return &stub{} }

func (s *stub) Transcribe(ctx context.Context, pcm []byte, sampleRate, channels int) (Transcript, error) {
	if len(pcm) == 0 {
		return Transcript{}, nil
	}
	return Transcript{Text: "deploy ke staging", Confidence: 1.0}, nil
}
