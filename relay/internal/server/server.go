//go:generate go run hermes-voice/cmd/contract

package server

import (
	"context"
	"crypto/rand"
	"crypto/subtle"
	"encoding/hex"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"strings"
	"sync"
	"time"

	"hermes-voice/internal/config"
	"hermes-voice/internal/firmware"
	"hermes-voice/internal/hermes"
	"hermes-voice/internal/mqtt"
	"hermes-voice/internal/stt"
)

type Server struct {
	cfg       config.Config
	stt       stt.Provider
	hermes    *hermes.Client
	mqtt      mqtt.Publisher
	fw        *firmware.Store
	mux       *http.ServeMux
	mu        sync.Mutex
	sessions  map[string]string
	cancels   map[string]context.CancelFunc
	cancelGen map[string]uint64
	chunks    *chunkStore
	stopSweep chan struct{}
}

func New(cfg config.Config, sttProv stt.Provider, h *hermes.Client, m mqtt.Publisher, fw *firmware.Store) *Server {
	s := &Server{cfg: cfg, stt: sttProv, hermes: h, mqtt: m, fw: fw,
		mux: http.NewServeMux(), sessions: map[string]string{}, cancels: map[string]context.CancelFunc{}, cancelGen: map[string]uint64{}}
	s.chunks = newChunkStore(stt.MaxPCMBytes, cfg.ChunkMaxCount, cfg.ChunkTTL)
	s.stopSweep = make(chan struct{})
	ticker := time.NewTicker(60 * time.Second)
	go func() {
		defer ticker.Stop()
		for {
			select {
			case <-ticker.C:
				s.chunks.sweep()
			case <-s.stopSweep:
				return
			}
		}
	}()
	s.mux.HandleFunc("POST /v1/voice", s.handleVoice)
	s.mux.HandleFunc("POST /v1/chunks/start", s.handleChunkStart)
	s.mux.HandleFunc("POST /v1/chunks/{id}", s.handleChunkAppend)
	s.mux.HandleFunc("POST /v1/chunks/{id}/finish", s.handleChunkFinish)
	s.mux.HandleFunc("POST /v1/cancel", s.handleCancel)
	s.mux.HandleFunc("GET /v1/firmware/version", s.handleFirmwareVersion)
	s.mux.HandleFunc("GET /v1/firmware/{name}", s.handleFirmwareBinary)
	s.mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) {
		w.Write([]byte("ok"))
	})
	return s
}

func (s *Server) Handler() http.Handler { return logRequests(s.mux) }

func (s *Server) Close() {
	select {
	case <-s.stopSweep:
	default:
		close(s.stopSweep)
	}
}

// statusCatcher records the status code errReply/handlers write, so the
// access line carries the outcome without correlating across log lines.
type statusCatcher struct {
	http.ResponseWriter
	status int
}

func (s *statusCatcher) WriteHeader(code int) {
	s.status = code
	s.ResponseWriter.WriteHeader(code)
}

func requestID(r *http.Request) string {
	if id := r.Header.Get("X-Request-ID"); id != "" {
		return id
	}
	var b [4]byte
	rand.Read(b[:])
	return "rq_" + hex.EncodeToString(b[:])
}

func logRequests(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		rid := requestID(r)
		w.Header().Set("X-Request-ID", rid)
		wrapped := &statusCatcher{ResponseWriter: w, status: 200}
		next.ServeHTTP(wrapped, r.WithContext(context.WithValue(r.Context(), ridKey{}, rid)))
		if r.URL.Path == "/healthz" {
			return
		}
		attrs := []any{
			"method", r.Method, "path", r.URL.Path,
			"device", r.Header.Get("X-Device-ID"),
			"req", rid,
			"status", wrapped.status,
			"ms", int(time.Since(start).Milliseconds()),
		}
		if v := wrapped.Header().Get("X-Error"); v != "" {
			attrs = append(attrs, "x-error", v)
		}
		if v := wrapped.Header().Get("X-Transcript"); v != "" {
			attrs = append(attrs, "transcript_len", len(v))
		}
		if v := wrapped.Header().Get("X-STT-MS"); v != "" {
			attrs = append(attrs, "stt_ms", v)
		}
		if v := wrapped.Header().Get("X-Hermes-MS"); v != "" {
			attrs = append(attrs, "hermes_ms", v)
		}
		if r.ContentLength > 0 {
			attrs = append(attrs, "pcm_bytes", r.ContentLength)
		}
		slog.Info("request", attrs...)
	})
}

func validDeviceID(id string) bool {
	if len(id) == 0 || len(id) > 32 {
		return false
	}
	for _, c := range id {
		ok := c == '-' || c == '_' ||
			(c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
		if !ok {
			return false
		}
	}
	return true
}

func (s *Server) authorized(r *http.Request) (string, bool) {
	id := r.Header.Get("X-Device-ID")
	if !validDeviceID(id) {
		return "", false
	}
	if len(s.cfg.DeviceTokens) == 0 {
		return id, true
	}
	want, ok := s.cfg.DeviceTokens[id]
	if !ok || want == "" {
		return "", false
	}
	got := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer ")
	if got == "" {
		return "", false
	}
	if subtle.ConstantTimeCompare([]byte(got), []byte(want)) != 1 {
		return "", false
	}
	return id, true
}

func (s *Server) errReply(w http.ResponseWriter, code int, xerr, msg string) {
	w.Header().Set("X-Error", xerr)
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.WriteHeader(code)
	fmt.Fprint(w, msg)
}

func (s *Server) handleVoice(w http.ResponseWriter, r *http.Request) {
	deviceID, ok := s.authorized(r)
	if !ok {
		s.errReply(w, http.StatusUnauthorized, "unauthorized", "bad device id or token")
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, stt.MaxPCMBytes+1024)
	pcm, err := io.ReadAll(r.Body)
	if err != nil {
		s.errReply(w, http.StatusBadRequest, "too_long", "audio too large (max 30 s)")
		return
	}
	if len(pcm) == 0 {
		s.mqtt.PublishState(deviceID, "idle")
		s.errReply(w, http.StatusOK, "silence", "empty audio")
		return
	}
	s.transcribeAndPrompt(w, r, deviceID, pcm)
}

func (s *Server) handleCancel(w http.ResponseWriter, r *http.Request) {
	deviceID, ok := s.authorized(r)
	if !ok {
		if id := r.URL.Query().Get("device_id"); id != "" {
			r.Header.Set("X-Device-ID", id)
			deviceID, ok = s.authorized(r)
		}
	}
	if !ok {
		s.errReply(w, http.StatusUnauthorized, "unauthorized", "bad device id or token")
		return
	}
	s.mu.Lock()
	cancel, ok := s.cancels[deviceID]
	if ok {
		delete(s.cancels, deviceID)
	}
	s.cancelGen[deviceID]++
	sessionID := s.sessions[deviceID]
	if len(s.sessions) > 1000 {
		clear(s.sessions)
	}
	if len(s.cancels) > 1000 {
		for id, c := range s.cancels {
			c()
			delete(s.cancels, id)
		}
	}
	s.mu.Unlock()
	s.chunks.dropDevice(deviceID)
	if ok {
		cancel()
	}
	if sessionID != "" {
		_ = s.hermes.Cancel(context.Background(), sessionID)
	}
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	fmt.Fprint(w, "cancelled")
}

func (s *Server) sessionFor(deviceID string) string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.sessions[deviceID]
}

func (s *Server) setSession(deviceID, sessionID string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.sessions[deviceID] = sessionID
}

type ridKey struct{}

func ridOf(ctx context.Context) string {
	if v, ok := ctx.Value(ridKey{}).(string); ok {
		return v
	}
	return ""
}

// handleFirmwareVersion answers the device version poll. Device identity
// reuses the voice gate (X-Device-ID header or device_id query); tokens
// enforced when configured. 204 = nothing eligible, not an error.
func (s *Server) handleFirmwareVersion(w http.ResponseWriter, r *http.Request) {
	if id := r.URL.Query().Get("device_id"); id != "" {
		r.Header.Set("X-Device-ID", id)
	}
	if _, ok := s.authorized(r); !ok {
		s.errReply(w, http.StatusUnauthorized, "unauthorized", "bad device id or token")
		return
	}
	s.fw.HandleVersion(w, r)
}

// handleFirmwareBinary serves the image bytes the manifest names.
func (s *Server) handleFirmwareBinary(w http.ResponseWriter, r *http.Request) {
	if id := r.URL.Query().Get("device_id"); id != "" {
		r.Header.Set("X-Device-ID", id)
	}
	if _, ok := s.authorized(r); !ok {
		s.errReply(w, http.StatusUnauthorized, "unauthorized", "bad device id or token")
		return
	}
	s.fw.HandleBinary(w, r)
}

// firmwareDir delegates to the store's own discovery.
func firmwareDir() string { return firmware.Dir() }
