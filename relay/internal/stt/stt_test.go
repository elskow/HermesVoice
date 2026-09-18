package stt

import (
	"context"
	"testing"

	"hermes-voice/internal/config"
)

func TestFactoryRejectsUnknown(t *testing.T) {
	if _, err := New(config.Config{STTProvider: "azure"}); err == nil {
		t.Fatal("expected error for unknown provider")
	}
}

func TestStubRoundTrip(t *testing.T) {
	p, err := New(config.Config{STTProvider: "stub"})
	if err != nil {
		t.Fatal(err)
	}
	tr, err := p.Transcribe(context.Background(), []byte{1, 2}, 16000, 1)
	if err != nil {
		t.Fatal(err)
	}
	if tr.Text == "" {
		t.Fatal("expected stub transcript")
	}
	tr, err = p.Transcribe(context.Background(), nil, 16000, 1)
	if err != nil {
		t.Fatal(err)
	}
	if tr.Text != "" {
		t.Fatalf("expected empty silence transcript, got %q", tr.Text)
	}
}
