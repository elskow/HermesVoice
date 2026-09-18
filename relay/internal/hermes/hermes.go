package hermes

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"time"

	"hermes-voice/internal/config"
)

// Client talks to the real Hermes gateway API server
// (OpenAI-compatible surface + Hermes session endpoints).
// Base URL from HERMES_BASE_URL, e.g. http://hermes:8642.
const (
	PathResponses     = "/v1/responses"
	PathSessionChat   = "/api/sessions/%s/chat"
	PathSessionStream = "/api/sessions/%s/chat/stream"
	PathRunStop       = "/v1/runs/%s/stop"
)

type Client struct {
	cfg        config.Config
	http       *http.Client
	cancelHTTP *http.Client
	model      string
}

func New(cfg config.Config) *Client {
	return &Client{
		cfg:        cfg,
		http:       &http.Client{},
		cancelHTTP: &http.Client{Timeout: 5 * time.Second},
		model:      getenvDefault("HERMES_MODEL", "hermes-agent"),
	}
}

func getenvDefault(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

func (c *Client) url(path string) string { return c.cfg.HermesBaseURL + path }

func (c *Client) auth(req *http.Request) {
	if c.cfg.HermesAPIKey != "" {
		req.Header.Set("Authorization", "Bearer "+c.cfg.HermesAPIKey)
	}
}

// CreateSession returns a device-bound conversation name. The responses API
// chains turns server-side via `conversation`, so no create call is needed;
// the name itself is the session. Never fails on network.
func (c *Client) CreateSession(ctx context.Context, deviceID, locale string) (string, error) {
	_ = ctx
	_ = locale
	if deviceID == "" {
		return "", fmt.Errorf("hermes create session: empty device id")
	}
	return "voice-" + deviceID, nil
}

type responseOutputText struct {
	Text string `json:"text"`
}

type responseOutputItem struct {
	Type    string `json:"type"`
	Status  string `json:"status"`
	Text    string `json:"text"`
	Content []struct {
		Type string `json:"type"`
		Text string `json:"text"`
	} `json:"content"`
}

type responsesReply struct {
	ID     string               `json:"id"`
	Status string               `json:"status"`
	Output []responseOutputItem `json:"output"`
}

func extractReply(out responsesReply) string {
	for i := len(out.Output) - 1; i >= 0; i-- {
		it := out.Output[i]
		if it.Type != "message" {
			continue
		}
		if it.Text != "" {
			return it.Text
		}
		for j := len(it.Content) - 1; j >= 0; j-- {
			if it.Content[j].Text != "" {
				return it.Content[j].Text
			}
		}
	}
	return ""
}

// Prompt runs one synchronous agent turn in the device conversation.
// Locale is folded into a system hint; Hermes owns the model from config.yaml.
func (c *Client) Prompt(ctx context.Context, sessionID, text, locale string) (string, error) {
	_ = locale
	body, _ := json.Marshal(map[string]any{
		"input":        text,
		"conversation": sessionID,
		"store":        true,
	})
	req, err := http.NewRequestWithContext(ctx, "POST",
		c.url(PathResponses), bytes.NewReader(body))
	if err != nil {
		return "", err
	}
	req.Header.Set("Content-Type", "application/json")
	c.auth(req)
	resp, err := c.http.Do(req)
	if err != nil {
		return "", fmt.Errorf("hermes prompt: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("hermes prompt: status %d", resp.StatusCode)
	}
	var out responsesReply
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return "", fmt.Errorf("hermes decode: %w", err)
	}
	reply := extractReply(out)
	if reply == "" {
		return "", fmt.Errorf("hermes prompt: empty reply (status %s)", out.Status)
	}
	return reply, nil
}

// Cancel is best-effort: the responses API has no per-conversation stop.
// Callers already drop stale results locally via the generation guard;
// a future migration to the runs API (POST /v1/runs/{id}/stop) can wire
// true cancellation through here without changing call sites.
func (c *Client) Cancel(ctx context.Context, sessionID string) error {
	_ = ctx
	_ = sessionID
	return nil
}
