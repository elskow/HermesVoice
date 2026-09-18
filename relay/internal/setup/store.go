package setup

import (
	"fmt"
	"os"
	"strings"
)

// Store reads/writes the .env file atomically. The file is the state;
// the UI is an editor, not a system of record.
type Store struct{ path string }

func NewStore(path string) *Store { return &Store{path: path} }

// Load returns current values keyed by field. Missing file = all empty
// (UI renders .env.example defaults as placeholders, not values).
func (s *Store) Load() map[string]string {
	out := map[string]string{}
	data, err := os.ReadFile(s.path)
	if err != nil {
		return out
	}
	for _, line := range strings.Split(string(data), "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		k, v, ok := strings.Cut(line, "=")
		if ok {
			out[strings.TrimSpace(k)] = strings.TrimSpace(v)
		}
	}
	return out
}

// Save validates then atomically replaces the file (temp + rename, never
// truncate-in-place: a crashed write cannot halve the config).
func (s *Store) Save(vals map[string]string) error {
	if v := strings.TrimSuffix(vals["HERMES_BASE_URL"], "/"); v == "" {
		return fmt.Errorf("HERMES_BASE_URL is required")
	} else if _, err := parseURL(v); err != nil {
		return fmt.Errorf("bad HERMES_BASE_URL: %w", err)
	}
	switch strings.ToLower(vals["STT_PROVIDER"]) {
	case "", "stub", "deepgram", "deepgram-stream", "whisper":
	default:
		return fmt.Errorf("unknown STT_PROVIDER %q", vals["STT_PROVIDER"])
	}
	if strings.ToLower(vals["STT_PROVIDER"]) == "deepgram" ||
		strings.ToLower(vals["STT_PROVIDER"]) == "deepgram-stream" {
		if vals["STT_DEEPGRAM_API_KEY"] == "" {
			return fmt.Errorf("STT_DEEPGRAM_API_KEY is required for deepgram")
		}
	}
	if strings.ToLower(vals["STT_PROVIDER"]) == "whisper" && vals["STT_WHISPER_URL"] == "" {
		return fmt.Errorf("STT_WHISPER_URL is required for whisper")
	}

	var b strings.Builder
	b.WriteString("# Managed by relay setup UI. Hand-editable, same format.\n")
	for _, f := range Fields {
		fmt.Fprintf(&b, "%s=%s\n", f.Key, vals[f.Key])
	}
	tmp := s.path + ".tmp"
	if err := os.WriteFile(tmp, []byte(b.String()), 0600); err != nil {
		return err
	}
	return os.Rename(tmp, s.path)
}
