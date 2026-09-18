package setup

import (
	"os"
	"path/filepath"
	"testing"
)

func TestSaveRejectsBadHermesURL(t *testing.T) {
	s := NewStore(filepath.Join(t.TempDir(), ".env"))
	err := s.Save(map[string]string{"HERMES_BASE_URL": "x", "STT_PROVIDER": "stub"})
	if err == nil {
		t.Fatal("want error for bad URL")
	}
}

func TestSaveRejectsUnknownProvider(t *testing.T) {
	s := NewStore(filepath.Join(t.TempDir(), ".env"))
	err := s.Save(map[string]string{"HERMES_BASE_URL": "http://h:1", "STT_PROVIDER": "nope"})
	if err == nil {
		t.Fatal("want error for unknown provider")
	}
}

func TestSaveRejectsDeepgramWithoutKey(t *testing.T) {
	s := NewStore(filepath.Join(t.TempDir(), ".env"))
	err := s.Save(map[string]string{"HERMES_BASE_URL": "http://h:1", "STT_PROVIDER": "deepgram"})
	if err == nil {
		t.Fatal("want error for missing key")
	}
}

func TestSaveRoundTrip(t *testing.T) {
	path := filepath.Join(t.TempDir(), ".env")
	s := NewStore(path)
	vals := map[string]string{"HERMES_BASE_URL": "http://h:1", "STT_PROVIDER": "stub", "PORT": "8081"}
	if err := s.Save(vals); err != nil {
		t.Fatal(err)
	}
	got := s.Load()
	if got["HERMES_BASE_URL"] != "http://h:1" || got["PORT"] != "8081" {
		t.Fatalf("round trip: %v", got)
	}
	if fi, _ := os.Stat(path); fi.Mode().Perm() != 0600 {
		t.Fatalf("mode = %o, want 600", fi.Mode().Perm())
	}
}
