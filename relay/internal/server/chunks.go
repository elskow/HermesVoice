package server

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"io"
	"log/slog"
	"net/http"

	"hermes-voice/internal/mqtt"
	"strconv"
	"strings"
	"sync"
	"time"
)

type chunkSession struct {
	deviceID string
	chunks   map[int][]byte
	total    int
	lastSeen time.Time
}

type chunkStore struct {
	mu       sync.Mutex
	sessions map[string]*chunkSession
	maxBytes int
	maxCount int
	ttl      time.Duration
}

func newChunkStore(maxBytes, maxCount int, ttl time.Duration) *chunkStore {
	return &chunkStore{
		sessions: map[string]*chunkSession{},
		maxBytes: maxBytes,
		maxCount: maxCount,
		ttl:      ttl,
	}
}

func newUploadID() string {
	var b [8]byte
	rand.Read(b[:])
	return "upl_" + hex.EncodeToString(b[:])
}

func (c *chunkStore) start(deviceID string) string {
	id := newUploadID()
	c.mu.Lock()
	defer c.mu.Unlock()
	c.sessions[id] = &chunkSession{deviceID: deviceID, chunks: map[int][]byte{}, lastSeen: time.Now()}
	return id
}

func (c *chunkStore) append(id, deviceID string, seq int, data []byte) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	sess, ok := c.sessions[id]
	if !ok || sess.deviceID != deviceID {
		return fmt.Errorf("unknown upload")
	}
	if seq < 0 || seq >= c.maxCount {
		return fmt.Errorf("seq out of range")
	}
	oldLen := len(sess.chunks[seq])
	sess.total += len(data) - oldLen
	if sess.total > c.maxBytes {
		sess.total -= len(data) - oldLen
		return fmt.Errorf("too_long")
	}
	sess.chunks[seq] = data
	sess.lastSeen = time.Now()
	return nil
}

func (c *chunkStore) assemble(id, deviceID string) ([]byte, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	sess, ok := c.sessions[id]
	if !ok || sess.deviceID != deviceID {
		return nil, fmt.Errorf("unknown upload")
	}
	if len(sess.chunks) == 0 {
		return nil, fmt.Errorf("silence")
	}
	max := -1
	for seq := range sess.chunks {
		if seq > max {
			max = seq
		}
	}
	for i := 0; i <= max; i++ {
		if _, ok := sess.chunks[i]; !ok {
			return nil, fmt.Errorf("gap at seq %d", i)
		}
	}
	var pcm []byte
	for i := 0; i <= max; i++ {
		pcm = append(pcm, sess.chunks[i]...)
	}
	delete(c.sessions, id)
	return pcm, nil
}

func (c *chunkStore) dropDevice(deviceID string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	for id, sess := range c.sessions {
		if sess.deviceID == deviceID {
			delete(c.sessions, id)
		}
	}
}

func (c *chunkStore) drop(id string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	delete(c.sessions, id)
}

func (c *chunkStore) sweep() {
	c.mu.Lock()
	defer c.mu.Unlock()
	now := time.Now()
	for id, sess := range c.sessions {
		if now.Sub(sess.lastSeen) > c.ttl {
			delete(c.sessions, id)
		}
	}
}

func (s *Server) handleChunkStart(w http.ResponseWriter, r *http.Request) {
	deviceID, ok := s.authorized(r)
	if !ok {
		s.errReply(w, http.StatusUnauthorized, "unauthorized", "bad device id or token")
		return
	}
	s.mqtt.PublishState(deviceID, "uploading")
	w.Header().Set("X-Upload-ID", s.chunks.start(deviceID))
	w.WriteHeader(http.StatusOK)
}

func (s *Server) handleChunkAppend(w http.ResponseWriter, r *http.Request) {
	deviceID, ok := s.authorized(r)
	if !ok {
		s.errReply(w, http.StatusUnauthorized, "unauthorized", "bad device id or token")
		return
	}
	id := r.PathValue("id")
	seq, err := strconv.Atoi(r.URL.Query().Get("seq"))
	if id == "" || err != nil {
		s.errReply(w, http.StatusBadRequest, "bad_request", "missing upload id or seq")
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, int64(s.chunks.maxBytes+1024))
	data, err := io.ReadAll(r.Body)
	if err != nil || len(data) == 0 {
		s.errReply(w, http.StatusBadRequest, "too_long", "chunk too large or empty")
		return
	}
	if err := s.chunks.append(id, deviceID, seq, data); err != nil {
		switch err.Error() {
		case "too_long":
			s.errReply(w, http.StatusRequestEntityTooLarge, "too_long", "upload exceeds cap")
		case "unknown upload":
			s.errReply(w, http.StatusNotFound, "unknown_upload", "unknown upload, restart session")
		default:
			s.errReply(w, http.StatusBadRequest, "bad_request", err.Error())
		}
		return
	}
	w.WriteHeader(http.StatusOK)
}

func (s *Server) handleChunkFinish(w http.ResponseWriter, r *http.Request) {
	deviceID, ok := s.authorized(r)
	if !ok {
		s.errReply(w, http.StatusUnauthorized, "unauthorized", "bad device id or token")
		return
	}
	id := r.PathValue("id")
	if id == "" {
		s.errReply(w, http.StatusBadRequest, "bad_request", "missing upload id")
		return
	}
	pcm, err := s.chunks.assemble(id, deviceID)
	if err != nil {
		switch err.Error() {
		case "silence":
			s.mqtt.PublishState(deviceID, "idle")
			s.errReply(w, http.StatusOK, "silence", "no speech detected")
		case "unknown upload":
			s.errReply(w, http.StatusNotFound, "unknown_upload", "unknown upload, restart session")
		default:
			s.errReply(w, http.StatusBadRequest, "upload_gap", err.Error()+", retry upload")
		}
		return
	}
	s.transcribeAndPrompt(w, r.WithContext(r.Context()), deviceID, pcm)
}

func headerSafe(s string, max int) string {
	s = strings.Map(func(r rune) rune {
		if r == '\n' || r == '\r' {
			return -1
		}
		return r
	}, s)
	if len(s) > max {
		s = s[:max]
	}
	return s
}

func (s *Server) transcribeAndPrompt(w http.ResponseWriter, r *http.Request, deviceID string, pcm []byte) {
	s.mqtt.PublishState(deviceID, "working")
	turn := mqtt.Turn{DeviceID: deviceID, PCMBytes: len(pcm), RequestID: ridOf(r.Context())}
	emit := func(verdict string) {
		turn.Verdict = verdict
		s.mqtt.PublishTurn(turn)
	}

	ctx, cancel := context.WithTimeout(r.Context(), s.cfg.HermesTimeout+s.cfg.STTTimeout)
	s.mu.Lock()
	s.cancelGen[deviceID]++
	gen := s.cancelGen[deviceID]
	if old, ok := s.cancels[deviceID]; ok {
		delete(s.cancels, deviceID)
		_ = old
	}
	s.cancels[deviceID] = cancel
	s.mu.Unlock()
	stale := func() bool {
		s.mu.Lock()
		defer s.mu.Unlock()
		return s.cancelGen[deviceID] != gen
	}
	defer func() {
		s.mu.Lock()
		if s.cancelGen[deviceID] == gen {
			delete(s.cancels, deviceID)
		}
		s.mu.Unlock()
		cancel()
	}()

	if stale() {
		emit("cancelled")
		s.errReply(w, http.StatusConflict, "cancelled", "superseded by newer request")
		return
	}
	sttStart := time.Now()
	tr, err := s.stt.Transcribe(ctx, pcm, 16000, 1)
	sttMs := int(time.Since(sttStart).Milliseconds())
	slog.Debug("stt done", "device", deviceID, "stt_ms", sttMs)
	if err != nil {
		if !stale() {
			s.mqtt.PublishState(deviceID, "idle")
		}
		emit("stt_failed")
		s.errReply(w, http.StatusBadGateway, "stt_failed", "stt: "+err.Error())
		return
	}
	if strings.TrimSpace(tr.Text) == "" {
		if !stale() {
			s.mqtt.PublishState(deviceID, "idle")
		}
		emit("silence")
		s.errReply(w, http.StatusOK, "silence", "no speech detected")
		return
	}
	s.mqtt.PublishPrompt(deviceID, tr.Text, s.sessionFor(deviceID))
	turn.Transcript = tr.Text
	turn.STTMs = sttMs

	if stale() {
		s.errReply(w, http.StatusConflict, "cancelled", "superseded by newer request")
		return
	}
	sessionID := s.sessionFor(deviceID)
	if sessionID == "" {
		sessionID, err = s.hermes.CreateSession(ctx, deviceID, s.cfg.STTLocale)
		if err != nil {
			if !stale() {
				s.mqtt.PublishState(deviceID, "idle")
			}
			emit("hermes_timeout")
			s.errReply(w, http.StatusBadGateway, "hermes_timeout", "agent unavailable, try again")
			return
		}
		s.setSession(deviceID, sessionID)
	}

	if stale() {
		s.errReply(w, http.StatusConflict, "cancelled", "superseded by newer request")
		return
	}
	hStart := time.Now()
	reply, err := s.hermes.Prompt(ctx, sessionID, tr.Text, s.cfg.STTLocale)
	hermesMs := int(time.Since(hStart).Milliseconds())
	slog.Debug("turn stages", "device", deviceID, "stt_ms", sttMs, "hermes_ms", hermesMs)
	if stale() {
		s.errReply(w, http.StatusConflict, "cancelled", "superseded by newer request")
		return
	}
	if err != nil {
		s.mqtt.PublishState(deviceID, "idle")
		if ctx.Err() == context.DeadlineExceeded {
			emit("hermes_timeout")
			s.errReply(w, http.StatusGatewayTimeout, "hermes_timeout", "agent timed out, try again")
			return
		}
		emit("hermes_timeout")
		s.errReply(w, http.StatusBadGateway, "hermes_timeout", "agent error, try again")
		return
	}
	s.mqtt.PublishResponse(deviceID, reply, sessionID)
	s.mqtt.PublishState(deviceID, "idle")
	turn.Reply = reply
	turn.SessionID = sessionID
	turn.HermesMs = hermesMs
	emit("ok")

	w.Header().Set("X-Session-ID", sessionID)
	w.Header().Set("X-Transcript", headerSafe(tr.Text, 200))
	w.Header().Set("X-STT-MS", fmt.Sprint(tr.LatencyMs))
	w.Header().Set("X-Hermes-MS", fmt.Sprint(hermesMs))
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	fmt.Fprint(w, reply)
}
