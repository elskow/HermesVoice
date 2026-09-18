package stt

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"time"

	"hermes-voice/internal/config"
)

// Whisper driver: any HTTP whisper server exposing
// POST {base}/v1/transcribe {"audio_b64","language"} -> {"text","confidence"}.
// Points at faster-whisper-server or compatible via STT_WHISPER_URL.
type whisper struct {
	baseURL string
	locale  string
	client  *http.Client
}

func newWhisper(cfg config.Config) *whisper {
	return &whisper{baseURL: cfg.WhisperURL, locale: cfg.STTLocale,
		client: &http.Client{Timeout: cfg.STTTimeout}}
}

func (w *whisper) Transcribe(ctx context.Context, pcm []byte, sampleRate, channels int) (Transcript, error) {
	start := time.Now()
	_ = sampleRate
	_ = channels
	body, _ := json.Marshal(map[string]string{
		"audio_b64": base64.StdEncoding.EncodeToString(pcm),
		"language":  w.locale,
	})
	req, err := http.NewRequestWithContext(ctx, "POST", w.baseURL+"/v1/transcribe", bytes.NewReader(body))
	if err != nil {
		return Transcript{}, err
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := w.client.Do(req)
	if err != nil {
		return Transcript{}, fmt.Errorf("whisper: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return Transcript{}, fmt.Errorf("whisper: status %d", resp.StatusCode)
	}
	var out struct {
		Text       string  `json:"text"`
		Confidence float64 `json:"confidence"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return Transcript{}, fmt.Errorf("whisper decode: %w", err)
	}
	return Transcript{Text: out.Text, Confidence: out.Confidence, LatencyMs: msSince(start)}, nil
}
