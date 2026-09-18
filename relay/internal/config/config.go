package config

import (
	"errors"
	"fmt"
	"net/url"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/joho/godotenv"
)

type Config struct {
	Port          string
	MQTTBrokerURL string

	HermesBaseURL string
	HermesAPIKey  string
	HermesTimeout time.Duration

	STTProvider   string
	STTTimeout    time.Duration
	STTLocale     string
	DeepgramKey   string
	DeepgramModel string
	WhisperURL    string

	DeviceTokens map[string]string

	ChunkMaxCount int
	ChunkTTL      time.Duration
}

func getenv(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

func parseTokens(s string) map[string]string {
	out := map[string]string{}
	for _, pair := range strings.Split(s, ",") {
		pair = strings.TrimSpace(pair)
		if pair == "" {
			continue
		}
		id, tok, ok := strings.Cut(pair, ":")
		if !ok {
			continue
		}
		out[strings.TrimSpace(id)] = strings.TrimSpace(tok)
	}
	return out
}

// Load resolves env with documented precedence: explicit process env
// wins, relay/.env fills the rest (dev.py relies on this: it exports
// overrides, then execs the binary from the repo root).
func Load() (Config, error) {
	_ = godotenv.Load(".env")
	_ = godotenv.Load("relay/.env")
	var c Config
	c.Port = getenv("PORT", "8081")
	c.MQTTBrokerURL = os.Getenv("MQTT_BROKER_URL")

	c.HermesBaseURL = strings.TrimSuffix(getenv("HERMES_BASE_URL", ""), "/")
	if c.HermesBaseURL == "" {
		return c, errors.New("HERMES_BASE_URL is required")
	}
	if _, err := url.ParseRequestURI(c.HermesBaseURL); err != nil {
		return c, fmt.Errorf("bad HERMES_BASE_URL: %w", err)
	}
	c.HermesAPIKey = os.Getenv("HERMES_API_KEY")

	timeoutS, _ := strconv.Atoi(getenv("HERMES_TIMEOUT_S", "300"))
	if timeoutS <= 0 {
		timeoutS = 300
	}
	c.HermesTimeout = time.Duration(timeoutS) * time.Second

	c.STTProvider = strings.ToLower(getenv("STT_PROVIDER", "stub"))
	switch c.STTProvider {
	case "deepgram", "deepgram-stream", "whisper", "stub":
	default:
		return c, fmt.Errorf("unknown STT_PROVIDER %q (want deepgram|deepgram-stream|whisper|stub)", c.STTProvider)
	}
	sttS, _ := strconv.Atoi(getenv("STT_TIMEOUT_S", "15"))
	if sttS <= 0 {
		sttS = 15
	}
	c.STTTimeout = time.Duration(sttS) * time.Second
	c.STTLocale = getenv("STT_LOCALE", "id")

	if c.STTProvider == "deepgram" || c.STTProvider == "deepgram-stream" {
		c.DeepgramKey = os.Getenv("STT_DEEPGRAM_API_KEY")
		if c.DeepgramKey == "" {
			return c, errors.New("STT_DEEPGRAM_API_KEY is required when STT_PROVIDER=deepgram")
		}
		c.DeepgramModel = getenv("STT_DEEPGRAM_MODEL", "nova-3")
	}
	if c.STTProvider == "whisper" {
		c.WhisperURL = strings.TrimSuffix(getenv("STT_WHISPER_URL", ""), "/")
		if c.WhisperURL == "" {
			return c, errors.New("STT_WHISPER_URL is required when STT_PROVIDER=whisper")
		}
	}

	c.DeviceTokens = parseTokens(os.Getenv("DEVICE_TOKENS"))

	c.ChunkMaxCount, _ = strconv.Atoi(getenv("CHUNK_MAX_COUNT", "120"))
	if c.ChunkMaxCount <= 0 {
		c.ChunkMaxCount = 120
	}
	ttlS, _ := strconv.Atoi(getenv("CHUNK_TTL_S", "300"))
	if ttlS <= 0 {
		ttlS = 300
	}
	c.ChunkTTL = time.Duration(ttlS) * time.Second
	return c, nil
}

// keySet reports presence without leaking values into logs.
func keySet(v string) string {
	if v != "" {
		return "set"
	}
	return ""
}

// Effective renders the resolved config for the boot log: one glance
// answers "why is it using stub?!". Secrets show presence only.
func (c Config) Effective() string {
	return "port=" + c.Port +
		" stt=" + c.STTProvider +
		" stt_model=" + c.DeepgramModel +
		" stt_key=" + keySet(c.DeepgramKey) +
		" hermes=" + c.HermesBaseURL +
		" hermes_key=" + keySet(c.HermesAPIKey) +
		" mqtt=" + c.MQTTBrokerURL +
		" tokens=" + keySet(mapKeys(c.DeviceTokens))
}

func mapKeys(m map[string]string) string {
	for k := range m {
		return k
	}
	return ""
}
