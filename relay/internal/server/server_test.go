package server

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"
	"time"

	"hermes-voice/internal/config"
	"hermes-voice/internal/firmware"
	"hermes-voice/internal/hermes"
	"hermes-voice/internal/mqtt"
	"hermes-voice/internal/stt"
)

type fakeSTT struct {
	text string
	err  error
}

func (f fakeSTT) Transcribe(ctx context.Context, pcm []byte, sr, ch int) (stt.Transcript, error) {
	if f.err != nil {
		return stt.Transcript{}, f.err
	}
	return stt.Transcript{Text: f.text, Confidence: 1}, nil
}

func testServer(t *testing.T, sttProv stt.Provider, hermesURL string) *Server {
	cfg := config.Config{
		Port: "0", HermesBaseURL: hermesURL,
		HermesTimeout: 5e9, STTTimeout: 5e9, STTLocale: "id",
		ChunkMaxCount: 120, ChunkTTL: 300000000000,
	}
	return New(cfg, sttProv, hermes.New(cfg), mqtt.LogPublisher{}, firmware.NewStore(t.TempDir()))
}

func fakeHermes(t *testing.T) *httptest.Server {
	t.Helper()
	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		switch {
		case r.URL.Path == "/v1/responses":
			io.WriteString(w, `{"id":"resp_t1","status":"completed","output":[{"type":"message","content":[{"type":"output_text","text":"ok reply"}]}]}`)
		default:
			http.NotFound(w, r)
		}
	}))
}

type turnCap struct {
	mqtt.LogPublisher
	turns []mqtt.Turn
}

func (c *turnCap) PublishTurn(t mqtt.Turn) { c.turns = append(c.turns, t) }

func testServerCap(t *testing.T, sttProv stt.Provider, hermesURL string) (*Server, *turnCap) {
	cfg := config.Config{
		Port: "0", HermesBaseURL: hermesURL,
		HermesTimeout: 5e9, STTTimeout: 5e9, STTLocale: "id",
		ChunkMaxCount: 120, ChunkTTL: 300000000000,
	}
	cap := &turnCap{}
	return New(cfg, sttProv, hermes.New(cfg), cap, firmware.NewStore(t.TempDir())), cap
}

func TestFirmwareVersionFlow(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "halo"}, h.URL)
	defer srv.Close()
	dir := t.TempDir()
	srv.fw = firmware.NewStore(dir)
	if _, err := srv.fw.Publish("9.9.9", []string{"node-01"},
		bytes.NewReader(bytes.Repeat([]byte{2}, 100))); err != nil {
		t.Fatal(err)
	}
	// Eligible device: 200 + manifest JSON.
	req := httptest.NewRequest("GET", "/v1/firmware/version?device_id=node-01", nil)
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)
	if rec.Code != 200 {
		t.Fatalf("eligible code = %d", rec.Code)
	}
	// Ineligible device: 204, no body.
	req = httptest.NewRequest("GET", "/v1/firmware/version?device_id=node-02", nil)
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)
	if rec.Code != 204 {
		t.Fatalf("ineligible code = %d", rec.Code)
	}
	// Binary: exact bytes back.
	req = httptest.NewRequest("GET", "/v1/firmware/voice-node-9.9.9.bin?device_id=node-01", nil)
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)
	if rec.Code != 200 || rec.Body.Len() != 100 {
		t.Fatalf("binary: %d bytes=%d", rec.Code, rec.Body.Len())
	}
}

func TestRequestID(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "halo"}, h.URL)
	defer srv.Close()

	req := httptest.NewRequest("POST", "/v1/voice", bytes.NewReader([]byte{1, 2, 3}))
	req.Header.Set("X-Device-ID", "node-01")
	req.Header.Set("X-Request-ID", "rq_test123")
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)
	if rec.Header().Get("X-Request-ID") != "rq_test123" {
		t.Fatalf("inbound id not echoed: %q", rec.Header().Get("X-Request-ID"))
	}

	srv2, cap := testServerCap(t, fakeSTT{text: "halo"}, h.URL)
	defer srv2.Close()
	rec2 := postVoice(t, srv2, "node-01", bytes.Repeat([]byte{7}, 4000))
	got := rec2.Header().Get("X-Request-ID")
	if got == "" || got[:3] != "rq_" {
		t.Fatalf("generated id missing: %q", got)
	}
	if len(cap.turns) != 1 || cap.turns[0].RequestID != got {
		t.Fatalf("turn/request mismatch: %+v vs %q", cap.turns, got)
	}
}

func TestTurnEnvelope(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv, cap := testServerCap(t, fakeSTT{text: "halo"}, h.URL)
	defer srv.Close()

	rec := postVoice(t, srv, "node-01", bytes.Repeat([]byte{7}, 4000))
	if rec.Code != 200 {
		t.Fatalf("voice: %d", rec.Code)
	}
	if len(cap.turns) != 1 {
		t.Fatalf("want 1 turn, got %d", len(cap.turns))
	}
	tr := cap.turns[0]
	if tr.Verdict != "ok" || tr.Transcript != "halo" || tr.Reply != "ok reply" {
		t.Fatalf("turn mismatch: %+v", tr)
	}
	if tr.DeviceID != "node-01" || tr.PCMBytes != 4000 || tr.SessionID == "" {
		t.Fatalf("turn envelope: %+v", tr)
	}

	rec = postVoice(t, srv, "node-01", []byte{})
	if rec.Header().Get("X-Error") != "silence" {
		t.Fatalf("want silence, got %d", rec.Code)
	}
	if len(cap.turns) != 1 {
		t.Fatalf("empty body is not a turn (no transcribe), got %d", len(cap.turns))
	}
}

func postVoice(t *testing.T, srv *Server, deviceID string, pcm []byte) *httptest.ResponseRecorder {
	t.Helper()
	req := httptest.NewRequest("POST", "/v1/voice", bytes.NewReader(pcm))
	req.Header.Set("X-Device-ID", deviceID)
	req.Header.Set("Content-Type", "audio/l16;rate=16000;channels=1")
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)
	return rec
}

func TestVoiceRoundTrip(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "halo"}, h.URL)
	defer srv.Close()

	rec := postVoice(t, srv, "node-01", bytes.Repeat([]byte{1}, 3200))
	if rec.Code != 200 {
		t.Fatalf("status=%d body=%q", rec.Code, rec.Body.String())
	}
	if rec.Header().Get("X-Transcript") != "halo" {
		t.Fatalf("missing transcript header: %v", rec.Header())
	}
	if rec.Header().Get("X-Session-ID") != "voice-node-01" {
		t.Fatalf("missing session header: %v", rec.Header())
	}
	if rec.Body.String() != "ok reply" {
		t.Fatalf("bad body: %q", rec.Body.String())
	}
}

func TestVoiceSilence(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "   "}, h.URL)
	defer srv.Close()

	rec := postVoice(t, srv, "node-01", bytes.Repeat([]byte{1}, 3200))
	if rec.Header().Get("X-Error") != "silence" {
		t.Fatalf("expected silence, got %d %q", rec.Code, rec.Body.String())
	}
}

func TestVoiceEmptyBody(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "x"}, h.URL)
	defer srv.Close()

	rec := postVoice(t, srv, "node-01", nil)
	if rec.Header().Get("X-Error") != "silence" {
		t.Fatalf("expected silence for empty body, got %d", rec.Code)
	}
}

func TestVoiceMissingDeviceID(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "x"}, h.URL)
	defer srv.Close()

	req := httptest.NewRequest("POST", "/v1/voice", bytes.NewReader([]byte{1}))
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)
	if rec.Code != 401 {
		t.Fatalf("expected 401, got %d", rec.Code)
	}
}

func TestVoiceWithDeviceTokens(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	cfg := config.Config{
		HermesBaseURL: h.URL, HermesTimeout: 5e9, STTTimeout: 5e9,
		STTLocale:     "id",
		DeviceTokens:  map[string]string{"node-01": "secret"},
		ChunkMaxCount: 120, ChunkTTL: 300000000000,
	}
	srv := New(cfg, fakeSTT{text: "x"}, hermes.New(cfg), mqtt.LogPublisher{}, firmware.NewStore(t.TempDir()))
	defer srv.Close()

	req := httptest.NewRequest("POST", "/v1/voice", bytes.NewReader(bytes.Repeat([]byte{1}, 100)))
	req.Header.Set("X-Device-ID", "node-01")
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)
	if rec.Code != 401 {
		t.Fatalf("expected 401 without token, got %d", rec.Code)
	}

	req = httptest.NewRequest("POST", "/v1/voice", bytes.NewReader(bytes.Repeat([]byte{1}, 100)))
	req.Header.Set("X-Device-ID", "node-01")
	req.Header.Set("Authorization", "Bearer secret")
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)
	if rec.Code != 200 {
		t.Fatalf("expected 200 with token, got %d %q", rec.Code, rec.Body.String())
	}
}

func TestChunkSessionRoundTrip(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "chunked halo"}, h.URL)
	defer srv.Close()

	start := httptest.NewRequest("POST", "/v1/chunks/start", nil)
	start.Header.Set("X-Device-ID", "node-01")
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, start)
	if rec.Code != 200 {
		t.Fatalf("start: %d", rec.Code)
	}
	upl := rec.Header().Get("X-Upload-ID")
	if upl == "" {
		t.Fatal("missing upload id")
	}

	full := bytes.Repeat([]byte{7}, 70000)
	for i, off := 0, 0; off < len(full); i, off = i+1, off+32768 {
		end := off + 32768
		if end > len(full) {
			end = len(full)
		}
		req := httptest.NewRequest("POST", "/v1/chunks/"+upl+"?seq="+itoa(i), bytes.NewReader(full[off:end]))
		req.Header.Set("X-Device-ID", "node-01")
		rec = httptest.NewRecorder()
		srv.Handler().ServeHTTP(rec, req)
		if rec.Code != 200 {
			t.Fatalf("chunk %d: %d", i, rec.Code)
		}
	}

	fin := httptest.NewRequest("POST", "/v1/chunks/"+upl+"/finish", nil)
	fin.Header.Set("X-Device-ID", "node-01")
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, fin)
	if rec.Code != 200 {
		t.Fatalf("finish: %d %q", rec.Code, rec.Body.String())
	}
	if rec.Header().Get("X-Transcript") != "chunked halo" {
		t.Fatalf("bad transcript: %v", rec.Header())
	}
}

func TestChunkGapRejected(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "x"}, h.URL)
	defer srv.Close()

	start := httptest.NewRequest("POST", "/v1/chunks/start", nil)
	start.Header.Set("X-Device-ID", "node-01")
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, start)
	upl := rec.Header().Get("X-Upload-ID")

	req := httptest.NewRequest("POST", "/v1/chunks/"+upl+"?seq=2", bytes.NewReader([]byte{1}))
	req.Header.Set("X-Device-ID", "node-01")
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)

	fin := httptest.NewRequest("POST", "/v1/chunks/"+upl+"/finish", nil)
	fin.Header.Set("X-Device-ID", "node-01")
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, fin)
	if rec.Code != 400 {
		t.Fatalf("expected 400 for gap, got %d", rec.Code)
	}
}

func itoa(i int) string {
	return fmt.Sprintf("%d", i)
}

// Relay append is idempotent per seq: a retried chunk POST overwrites
// instead of duplicating bytes.
func TestChunkRepostOverwrites(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "x"}, h.URL)
	defer srv.Close()

	start := httptest.NewRequest("POST", "/v1/chunks/start", nil)
	start.Header.Set("X-Device-ID", "node-01")
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, start)
	upl := rec.Header().Get("X-Upload-ID")
	if upl == "" {
		t.Fatal("missing upload id")
	}

	post := func(seq int, body []byte) *httptest.ResponseRecorder {
		req := httptest.NewRequest("POST", "/v1/chunks/"+upl+"?seq="+itoa(seq), bytes.NewReader(body))
		req.Header.Set("X-Device-ID", "node-01")
		rec := httptest.NewRecorder()
		srv.Handler().ServeHTTP(rec, req)
		return rec
	}
	if rec := post(0, []byte{1, 2}); rec.Code != 200 {
		t.Fatalf("seq0: %d", rec.Code)
	}
	if rec := post(0, []byte{1, 2, 3}); rec.Code != 200 {
		t.Fatalf("seq0 repost: %d", rec.Code)
	}

	fin := httptest.NewRequest("POST", "/v1/chunks/"+upl+"/finish", nil)
	fin.Header.Set("X-Device-ID", "node-01")
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, fin)
	if rec.Code != 200 {
		t.Fatalf("finish after repost: %d (%s)", rec.Code, rec.Body.String())
	}
}

func TestCancelDropsChunks(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "x"}, h.URL)
	defer srv.Close()

	start := httptest.NewRequest("POST", "/v1/chunks/start", nil)
	start.Header.Set("X-Device-ID", "node-01")
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, start)
	upl := rec.Header().Get("X-Upload-ID")
	if upl == "" {
		t.Fatal("missing upload id")
	}

	cancel := httptest.NewRequest("POST", "/v1/cancel?device_id=node-01", nil)
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, cancel)

	fin := httptest.NewRequest("POST", "/v1/chunks/"+upl+"/finish", nil)
	fin.Header.Set("X-Device-ID", "node-01")
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, fin)
	if rec.Code == 200 {
		t.Fatalf("expected error after cancel-drop, got 200 %q", rec.Body.String())
	}
}

func TestBadDeviceIDRejected(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "x"}, h.URL)
	defer srv.Close()
	defer srv.Close()

	for _, id := range []string{"../escape", "node/01", "NODE-01", "a very long device id over thirty two chars!!"} {
		req := httptest.NewRequest("POST", "/v1/voice", bytes.NewReader([]byte{1}))
		req.Header.Set("X-Device-ID", id)
		rec := httptest.NewRecorder()
		srv.Handler().ServeHTTP(rec, req)
		if rec.Code != 401 {
			t.Fatalf("id %q: expected 401, got %d", id, rec.Code)
		}
	}
}

func TestServerCloseIdempotent(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "x"}, h.URL)
	defer srv.Close()
	srv.Close()
	srv.Close()
}

func TestEmptyTokenFailClosed(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	cfg := config.Config{
		HermesBaseURL: h.URL, HermesTimeout: 5e9, STTTimeout: 5e9,
		STTLocale:     "id",
		DeviceTokens:  map[string]string{"node-01": ""},
		ChunkMaxCount: 120, ChunkTTL: 300000000000,
	}
	srv := New(cfg, fakeSTT{text: "x"}, hermes.New(cfg), mqtt.LogPublisher{}, firmware.NewStore(t.TempDir()))
	defer srv.Close()

	req := httptest.NewRequest("POST", "/v1/voice", bytes.NewReader([]byte{1}))
	req.Header.Set("X-Device-ID", "node-01")
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)
	if rec.Code != 401 {
		t.Fatalf("expected 401 for empty-token device, got %d", rec.Code)
	}
}

func TestHeaderSafe(t *testing.T) {
	if got := headerSafe("a\nb\rc", 200); got != "abc" {
		t.Fatalf("CR/LF not stripped: %q", got)
	}
	long := string(make([]byte, 500))
	for i := range []byte(long) {
		_ = i
	}
	if got := headerSafe(long, 200); len(got) != 200 {
		t.Fatalf("not truncated: %d", len(got))
	}
}

func TestSupersededRequest(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, &blockingSTT{release: make(chan struct{})}, h.URL)
	defer srv.Close()

	done1 := make(chan *httptest.ResponseRecorder, 1)
	go func() {
		req := httptest.NewRequest("POST", "/v1/voice", bytes.NewReader(bytes.Repeat([]byte{1}, 100)))
		req.Header.Set("X-Device-ID", "node-01")
		rec := httptest.NewRecorder()
		srv.Handler().ServeHTTP(rec, req)
		done1 <- rec
	}()
	time.Sleep(50 * time.Millisecond)

	rec2 := postVoice(t, srv, "node-01", bytes.Repeat([]byte{1}, 100))
	if rec2.Code != 200 {
		t.Fatalf("second request: got %d", rec2.Code)
	}
	close(srv.stt.(*blockingSTT).release)
	rec1 := <-done1
	if rec1.Code != 409 || rec1.Header().Get("X-Error") != "cancelled" {
		t.Fatalf("stale request: got %d %q", rec1.Code, rec1.Header().Get("X-Error"))
	}
}

type blockingSTT struct {
	release chan struct{}
	n       int32
}

func (b *blockingSTT) Transcribe(ctx context.Context, pcm []byte, sr, ch int) (stt.Transcript, error) {
	if atomic.AddInt32(&b.n, 1) == 1 {
		select {
		case <-b.release:
			return stt.Transcript{Text: "late"}, nil
		case <-ctx.Done():
			return stt.Transcript{}, ctx.Err()
		}
	}
	return stt.Transcript{Text: "fresh"}, nil
}

func TestCancelRequiresAuth(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	cfg := config.Config{
		HermesBaseURL: h.URL, HermesTimeout: 5e9, STTTimeout: 5e9,
		STTLocale:     "id",
		DeviceTokens:  map[string]string{"node-01": "secret"},
		ChunkMaxCount: 120, ChunkTTL: 300000000000,
	}
	srv := New(cfg, fakeSTT{text: "x"}, hermes.New(cfg), mqtt.LogPublisher{}, firmware.NewStore(t.TempDir()))
	defer srv.Close()

	anon := httptest.NewRequest("POST", "/v1/cancel?device_id=node-01", nil)
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, anon)
	if rec.Code != 401 {
		t.Fatalf("expected 401 for anonymous cancel, got %d", rec.Code)
	}

	Authed := httptest.NewRequest("POST", "/v1/cancel", nil)
	Authed.Header.Set("X-Device-ID", "node-01")
	Authed.Header.Set("Authorization", "Bearer secret")
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, Authed)
	if rec.Body.String() != "cancelled" {
		t.Fatalf("expected cancelled with token, got %d %q", rec.Code, rec.Body.String())
	}
}

func TestChunkErrorTaxonomy(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "x"}, h.URL)
	defer srv.Close()

	fin := httptest.NewRequest("POST", "/v1/chunks/nope/finish", nil)
	fin.Header.Set("X-Device-ID", "node-01")
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, fin)
	if rec.Code != 404 || rec.Header().Get("X-Error") != "unknown_upload" {
		t.Fatalf("finish unknown: got %d %q", rec.Code, rec.Header().Get("X-Error"))
	}

	start := httptest.NewRequest("POST", "/v1/chunks/start", nil)
	start.Header.Set("X-Device-ID", "node-01")
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, start)
	upl := rec.Header().Get("X-Upload-ID")

	bad := httptest.NewRequest("POST", "/v1/chunks/"+upl+"?seq=9999", bytes.NewReader([]byte{1}))
	bad.Header.Set("X-Device-ID", "node-01")
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, bad)
	if rec.Code != 400 || rec.Header().Get("X-Error") != "bad_request" {
		t.Fatalf("seq range: got %d %q", rec.Code, rec.Header().Get("X-Error"))
	}
}

func TestCancel(t *testing.T) {
	h := fakeHermes(t)
	defer h.Close()
	srv := testServer(t, fakeSTT{text: "x"}, h.URL)
	defer srv.Close()

	req := httptest.NewRequest("POST", "/v1/cancel?device_id=node-01", nil)
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)
	if rec.Body.String() != "cancelled" {
		t.Fatalf("bad cancel body: %q", rec.Body.String())
	}
}
