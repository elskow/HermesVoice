package setup

import (
	"bytes"
	"context"
	"fmt"
	"net/http"
	"strings"
	"time"
)

// ProbeResult names which provider was tested and whether it answered.
type ProbeResult struct {
	Provider string `json:"provider"`
	OK       bool   `json:"ok"`
	Detail   string `json:"detail"`
}

// ProbeHermes hits the gateway models endpoint with the candidate key.
func ProbeHermes(baseURL, apiKey string) ProbeResult {
	ctx, cancel := context.WithTimeout(context.Background(), 8*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, "GET",
		strings.TrimSuffix(baseURL, "/")+"/v1/models", nil)
	if err != nil {
		return ProbeResult{"hermes", false, err.Error()}
	}
	if apiKey != "" {
		req.Header.Set("Authorization", "Bearer "+apiKey)
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return ProbeResult{"hermes", false, err.Error()}
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return ProbeResult{"hermes", false, fmt.Sprintf("status %d", resp.StatusCode)}
	}
	return ProbeResult{"hermes", true, "gateway answered"}
}

// ProbeDeepgram posts one second of silence; any non-401 is a live key
// (401 = bad key, everything else = reachable + accepted).
func ProbeDeepgram(apiKey string) ProbeResult {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	silence := make([]byte, 32000)
	req, err := http.NewRequestWithContext(ctx, "POST",
		"https://api.deepgram.com/v1/listen?model=nova-3&encoding=linear16&sample_rate=16000&channels=1",
		bytes.NewReader(silence))
	if err != nil {
		return ProbeResult{"deepgram", false, err.Error()}
	}
	req.Header.Set("Authorization", "Token "+apiKey)
	req.Header.Set("Content-Type", "audio/l16")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return ProbeResult{"deepgram", false, err.Error()}
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusUnauthorized {
		return ProbeResult{"deepgram", false, "key rejected (401)"}
	}
	if resp.StatusCode != http.StatusOK {
		return ProbeResult{"deepgram", false, fmt.Sprintf("status %d", resp.StatusCode)}
	}
	return ProbeResult{"deepgram", true, "key accepted"}
}

// ProbeWhisper hits the server root; reachable = configured correctly
// (the transcribe shape is validated on first real turn).
func ProbeWhisper(baseURL string) ProbeResult {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, "GET", strings.TrimSuffix(baseURL, "/")+"/", nil)
	if err != nil {
		return ProbeResult{"whisper", false, err.Error()}
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return ProbeResult{"whisper", false, err.Error()}
	}
	defer resp.Body.Close()
	return ProbeResult{"whisper", true, fmt.Sprintf("reachable (status %d)", resp.StatusCode)}
}
