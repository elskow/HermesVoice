package config

import (
	"testing"
	"time"
)

func setenv(t *testing.T, k, v string) {
	t.Helper()
	t.Setenv(k, v)
}

func baseEnv(t *testing.T) {
	t.Helper()
	setenv(t, "HERMES_BASE_URL", "http://hermes:8080")
	setenv(t, "HERMES_API_KEY", "")
	setenv(t, "HERMES_TIMEOUT_S", "")
	setenv(t, "STT_PROVIDER", "stub")
	setenv(t, "STT_TIMEOUT_S", "")
	setenv(t, "STT_LOCALE", "")
	setenv(t, "STT_DEEPGRAM_API_KEY", "")
	setenv(t, "STT_WHISPER_URL", "")
	setenv(t, "DEVICE_TOKENS", "")
	setenv(t, "PORT", "")
	setenv(t, "MQTT_BROKER_URL", "")
}

func TestDefaults(t *testing.T) {
	baseEnv(t)
	c, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if c.Port != "8081" || c.STTProvider != "stub" || c.STTLocale != "id" {
		t.Fatalf("bad defaults: %+v", c)
	}
	if c.HermesTimeout != 300*time.Second || c.STTTimeout != 15*time.Second {
		t.Fatalf("bad timeout defaults: %+v", c)
	}
	if len(c.DeviceTokens) != 0 {
		t.Fatalf("expected no tokens: %+v", c)
	}
}

func TestMissingHermesBaseURL(t *testing.T) {
	baseEnv(t)
	setenv(t, "HERMES_BASE_URL", "")
	if _, err := Load(); err == nil {
		t.Fatal("expected error for missing HERMES_BASE_URL")
	}
}

func TestBadHermesBaseURL(t *testing.T) {
	baseEnv(t)
	setenv(t, "HERMES_BASE_URL", "://bad")
	if _, err := Load(); err == nil {
		t.Fatal("expected error for malformed HERMES_BASE_URL")
	}
}

func TestDeepgramStreamSharesKey(t *testing.T) {
	baseEnv(t)
	setenv(t, "STT_PROVIDER", "deepgram-stream")
	if _, err := Load(); err == nil {
		t.Fatal("expected error for deepgram-stream without key")
	}
	setenv(t, "STT_DEEPGRAM_API_KEY", "k")
	if _, err := Load(); err != nil {
		t.Fatal(err)
	}
}

func TestUnknownSTTProvider(t *testing.T) {
	baseEnv(t)
	setenv(t, "STT_PROVIDER", "azure")
	if _, err := Load(); err == nil {
		t.Fatal("expected error for unknown STT_PROVIDER")
	}
}

func TestDeepgramRequiresKey(t *testing.T) {
	baseEnv(t)
	setenv(t, "STT_PROVIDER", "deepgram")
	if _, err := Load(); err == nil {
		t.Fatal("expected error for deepgram without key")
	}
	setenv(t, "STT_DEEPGRAM_API_KEY", "k")
	c, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if c.DeepgramModel != "nova-3" {
		t.Fatalf("expected nova-3 default, got %q", c.DeepgramModel)
	}
}

func TestWhisperRequiresURL(t *testing.T) {
	baseEnv(t)
	setenv(t, "STT_PROVIDER", "whisper")
	if _, err := Load(); err == nil {
		t.Fatal("expected error for whisper without url")
	}
	setenv(t, "STT_WHISPER_URL", "http://w:8000/")
	c, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if c.WhisperURL != "http://w:8000" {
		t.Fatalf("expected trailing slash trimmed, got %q", c.WhisperURL)
	}
}

func TestChunkDefaults(t *testing.T) {
	baseEnv(t)
	c, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if c.ChunkMaxCount != 120 {
		t.Fatalf("expected 120 chunks, got %d", c.ChunkMaxCount)
	}
	if c.ChunkTTL.Seconds() != 300 {
		t.Fatalf("expected 300s ttl, got %v", c.ChunkTTL)
	}
}

func TestDeviceTokens(t *testing.T) {
	baseEnv(t)
	setenv(t, "DEVICE_TOKENS", "node-01:tok1, node-02:tok2")
	c, err := Load()
	if err != nil {
		t.Fatal(err)
	}
	if c.DeviceTokens["node-01"] != "tok1" || c.DeviceTokens["node-02"] != "tok2" {
		t.Fatalf("bad tokens: %+v", c.DeviceTokens)
	}
}
