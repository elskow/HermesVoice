package status

import (
	"testing"

	"hermes-voice/internal/mqtt"
)

type stubPub struct{ turns []mqtt.Turn }

func (s *stubPub) PublishState(_, _ string)       {}
func (s *stubPub) PublishPrompt(_, _, _ string)   {}
func (s *stubPub) PublishResponse(_, _, _ string) {}
func (s *stubPub) PublishTurn(t mqtt.Turn)        { s.turns = append(s.turns, t) }

func TestRecorderCountsAndCaps(t *testing.T) {
	down := &stubPub{}
	r := NewRecorder(down, 3)
	for i, v := range []string{"ok", "ok", "silence", "ok", "stt_failed"} {
		r.PublishTurn(mqtt.Turn{DeviceID: "d", Verdict: v, Transcript: string(rune('a' + i))})
	}
	if len(down.turns) != 5 {
		t.Fatalf("downstream got %d turns, want 5 (recording must not swallow)", len(down.turns))
	}
	snap := r.Snapshot()
	if snap.Verdicts["ok"] != 3 || snap.Verdicts["silence"] != 1 || snap.Verdicts["stt_failed"] != 1 {
		t.Fatalf("counters: %v", snap.Verdicts)
	}
	if len(snap.Recent) != 3 {
		t.Fatalf("recent len = %d, want cap 3", len(snap.Recent))
	}
	if snap.Recent[0].Verdict != "stt_failed" {
		t.Fatalf("newest first: got %q", snap.Recent[0].Verdict)
	}
}
