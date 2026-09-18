package stt

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/url"
	"time"

	"hermes-voice/internal/config"
)

// Deepgram pre-recorded driver (external service, zero local knowledge).
// Verified 2026-09-15 against developers.deepgram.com:
//
//	endpoint POST https://api.deepgram.com/v1/listen, raw PCM body
//	auth "Authorization: Token <key>" (NOT Bearer; Bearer is JWT-only)
//	raw PCM requires encoding=linear16 AND sample_rate together
//	transcript at results.channels[0].alternatives[0].transcript
type deepgram struct {
	key    string
	model  string
	locale string
	client *http.Client
}

func newDeepgram(cfg config.Config) *deepgram {
	return &deepgram{
		key:    cfg.DeepgramKey,
		model:  cfg.DeepgramModel,
		locale: cfg.STTLocale,
		client: &http.Client{Timeout: cfg.STTTimeout},
	}
}

func (d *deepgram) Transcribe(ctx context.Context, pcm []byte, sampleRate, channels int) (Transcript, error) {
	start := time.Now()
	q := url.Values{}
	q.Set("model", d.model)
	q.Set("language", d.locale)
	q.Set("smart_format", "true")
	q.Set("punctuate", "true")
	q.Set("encoding", "linear16")
	q.Set("sample_rate", fmt.Sprint(sampleRate))
	q.Set("channels", fmt.Sprint(channels))

	req, err := http.NewRequestWithContext(ctx, "POST",
		"https://api.deepgram.com/v1/listen?"+q.Encode(), bytes.NewReader(pcm))
	if err != nil {
		return Transcript{}, err
	}
	req.Header.Set("Authorization", "Token "+d.key)
	req.Header.Set("Content-Type", "audio/l16")

	resp, err := d.client.Do(req)
	if err != nil {
		return Transcript{}, fmt.Errorf("deepgram: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return Transcript{}, fmt.Errorf("deepgram: status %d", resp.StatusCode)
	}
	var out struct {
		Results struct {
			Channels []struct {
				Alternatives []struct {
					Transcript string  `json:"transcript"`
					Confidence float64 `json:"confidence"`
				} `json:"alternatives"`
			} `json:"channels"`
		} `json:"results"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return Transcript{}, fmt.Errorf("deepgram decode: %w", err)
	}
	if len(out.Results.Channels) == 0 || len(out.Results.Channels[0].Alternatives) == 0 {
		return Transcript{LatencyMs: msSince(start)}, nil
	}
	alt := out.Results.Channels[0].Alternatives[0]
	return Transcript{Text: alt.Transcript, Confidence: alt.Confidence, LatencyMs: msSince(start)}, nil
}

func msSince(t time.Time) int { return int(time.Since(t).Milliseconds()) }
