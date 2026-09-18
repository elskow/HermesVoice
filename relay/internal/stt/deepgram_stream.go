package stt

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/url"
	"strings"
	"time"

	"hermes-voice/internal/config"
	"nhooyr.io/websocket"
)

// Deepgram streaming driver (external service, zero local knowledge).
// Verified 2026-09-15 against developers.deepgram.com/reference/speech-to-text/listen-streaming:
//
//	WS wss://api.deepgram.com/v1/listen, same query params as pre-recorded
//	auth "Authorization: Token <key>" header on the WS handshake
//	client sends binary PCM frames; server sends JSON Results messages
//	Finalize control message {"type":"Finalize"} flushes the last hypothesis
//	result selection: last message with is_final=true wins, else longest
//	  interim transcript. is_final + speech_final both true = utterance done.
type deepgramStream struct {
	key    string
	model  string
	locale string
	client *http.Client
}

func newDeepgramStream(cfg config.Config) *deepgramStream {
	return &deepgramStream{
		key:    cfg.DeepgramKey,
		model:  cfg.DeepgramModel,
		locale: cfg.STTLocale,
		client: &http.Client{Timeout: cfg.STTTimeout},
	}
}

func (d *deepgramStream) Transcribe(ctx context.Context, pcm []byte, sampleRate, channels int) (Transcript, error) {
	start := time.Now()
	q := url.Values{}
	q.Set("model", d.model)
	q.Set("language", d.locale)
	q.Set("smart_format", "true")
	q.Set("punctuate", "true")
	q.Set("encoding", "linear16")
	q.Set("sample_rate", fmt.Sprint(sampleRate))
	q.Set("channels", fmt.Sprint(channels))
	q.Set("endpointing", "300")
	q.Set("utterance_end_ms", "1000")

	dialCtx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	conn, _, err := websocket.Dial(dialCtx, "wss://api.deepgram.com/v1/listen?"+q.Encode(), &websocket.DialOptions{
		HTTPHeader: http.Header{"Authorization": []string{"Token " + d.key}},
	})
	if err != nil {
		return Transcript{}, fmt.Errorf("deepgram-stream dial: %w", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "")

	if err := conn.Write(ctx, websocket.MessageBinary, pcm); err != nil {
		return Transcript{}, fmt.Errorf("deepgram-stream write: %w", err)
	}
	if err := conn.Write(ctx, websocket.MessageText, []byte(`{"type":"Finalize"}`)); err != nil {
		return Transcript{}, fmt.Errorf("deepgram-stream finalize: %w", err)
	}

	var best Transcript
	var bestFinal bool
	sawResults := false
	deadline := time.Now().Add(10 * time.Second)
	for time.Now().Before(deadline) {
		_, raw, err := conn.Read(ctx)
		if err != nil {
			break
		}
		var msg struct {
			Type      string `json:"type"`
			IsFinal   bool   `json:"is_final"`
			SpeechFin bool   `json:"speech_final"`
			FromFin   bool   `json:"from_finalize"`
			Channel   struct {
				Alternatives []struct {
					Transcript string  `json:"transcript"`
					Confidence float64 `json:"confidence"`
				} `json:"alternatives"`
			} `json:"channel"`
		}
		if err := json.Unmarshal(raw, &msg); err != nil {
			continue
		}
		if msg.Type != "Results" || len(msg.Channel.Alternatives) == 0 {
			continue
		}
		sawResults = true
		alt := msg.Channel.Alternatives[0]
		text := strings.TrimSpace(alt.Transcript)
		if text == "" {
			continue
		}
		if msg.IsFinal && !bestFinal {
			best = Transcript{Text: text, Confidence: alt.Confidence}
			bestFinal = true
		} else if !bestFinal && len(text) > len(best.Text) {
			best = Transcript{Text: text, Confidence: alt.Confidence}
		}
		if msg.FromFin || (msg.IsFinal && msg.SpeechFin) {
			break
		}
	}
	best.LatencyMs = int(time.Since(start).Milliseconds())
	if best.Text == "" && !sawResults {
		return best, fmt.Errorf("deepgram-stream: no results before deadline")
	}
	return best, nil
}
