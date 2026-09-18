package firmware

import (
	"bytes"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
)

func TestEligible(t *testing.T) {
	open := Manifest{Version: "1"}
	if !open.Eligible("node-99") {
		t.Fatal("empty allowlist should admit everyone")
	}
	staged := Manifest{Version: "1", Devices: []string{"node-01"}}
	if !staged.Eligible("node-01") || staged.Eligible("node-02") {
		t.Fatal("allowlist should admit node-01 only")
	}
}

func TestPublishRoundTrip(t *testing.T) {
	s := NewStore(filepath.Join(t.TempDir(), "fw"))
	m, err := s.Publish("1.2.3", []string{"node-01"}, bytes.NewReader([]byte("fake-image")))
	if err != nil {
		t.Fatal(err)
	}
	if m.URL != "/v1/firmware/voice-node-1.2.3.bin" || len(m.SHA256) != 64 {
		t.Fatalf("manifest: %+v", m)
	}
	// Version poll: eligible device gets JSON, other gets 204.
	req := httptest.NewRequest("GET", "/v1/firmware/version?device_id=node-01", nil)
	rec := httptest.NewRecorder()
	s.HandleVersion(rec, req)
	if rec.Code != 200 {
		t.Fatalf("eligible code = %d", rec.Code)
	}
	req = httptest.NewRequest("GET", "/v1/firmware/version?device_id=node-02", nil)
	rec = httptest.NewRecorder()
	s.HandleVersion(rec, req)
	if rec.Code != 204 {
		t.Fatalf("ineligible code = %d", rec.Code)
	}
	// Binary serves back the exact bytes (device hashes these).
	mux := http.NewServeMux()
	mux.HandleFunc("GET /v1/firmware/{name}", s.HandleBinary)
	req = httptest.NewRequest("GET", "/v1/firmware/voice-node-1.2.3.bin", nil)
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, req)
	if rec.Code != 200 || rec.Body.String() != "fake-image" {
		t.Fatalf("binary: %d %q", rec.Code, rec.Body.String())
	}
	// Traversal rejected.
	req = httptest.NewRequest("GET", "/v1/firmware/..%2fsecret", nil)
	rec = httptest.NewRecorder()
	mux.ServeHTTP(rec, req)
	if rec.Code == 200 {
		t.Fatal("traversal served")
	}
}
