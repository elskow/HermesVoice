package hermes

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"hermes-voice/internal/config"
)

func testClient(srv *httptest.Server) *Client {
	return New(config.Config{
		HermesBaseURL: srv.URL,
		HermesAPIKey:  "k",
	})
}

func TestCreateSessionIsLocal(t *testing.T) {
	c := New(config.Config{})
	id, err := c.CreateSession(t.Context(), "node-01", "id")
	if err != nil || id != "voice-node-01" {
		t.Fatalf("got %q, %v", id, err)
	}
	if _, err := c.CreateSession(t.Context(), "", "id"); err == nil {
		t.Fatal("expected error for empty device id")
	}
}

func TestPromptExtractsMessageText(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/v1/responses" {
			t.Errorf("unexpected path %s", r.URL.Path)
		}
		var req struct {
			Input        string `json:"input"`
			Conversation string `json:"conversation"`
		}
		json.NewDecoder(r.Body).Decode(&req)
		if req.Conversation != "voice-node-01" {
			t.Errorf("conversation = %q", req.Conversation)
		}
		if !strings.Contains(req.Input, "deploy") {
			t.Errorf("input = %q", req.Input)
		}
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(`{"id":"resp_1","status":"completed","output":[` +
			`{"type":"function_call","status":"completed","name":"terminal"},` +
			`{"type":"message","content":[{"type":"output_text","text":"Done."}]}]}`))
	}))
	defer srv.Close()

	reply, err := testClient(srv).Prompt(t.Context(), "voice-node-01", "deploy", "id")
	if err != nil {
		t.Fatal(err)
	}
	if reply != "Done." {
		t.Fatalf("reply = %q", reply)
	}
}

func TestPromptEmptyReplyErrors(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(`{"id":"resp_1","status":"completed","output":[]}`))
	}))
	defer srv.Close()

	if _, err := testClient(srv).Prompt(t.Context(), "voice-node-01", "hi", "en"); err == nil {
		t.Fatal("expected error for empty reply")
	}
}

func TestPromptNon200Errors(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusUnauthorized)
	}))
	defer srv.Close()

	if _, err := testClient(srv).Prompt(t.Context(), "voice-node-01", "hi", "en"); err == nil {
		t.Fatal("expected error for 401")
	}
}

func TestCancelIsNoop(t *testing.T) {
	c := New(config.Config{})
	if err := c.Cancel(t.Context(), "voice-node-01"); err != nil {
		t.Fatalf("cancel should be no-op, got %v", err)
	}
}
