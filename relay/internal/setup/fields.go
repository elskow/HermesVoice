package setup

// Field describes one .env line for the setup UI. Order = file order.
type Field struct {
	Key     string
	Label   string
	Secret  bool
	Default string
	Hint    string
}

var Fields = []Field{
	{Key: "HERMES_BASE_URL", Label: "Hermes gateway URL", Default: "http://127.0.0.1:8642", Hint: "API server with key enabled"},
	{Key: "HERMES_API_KEY", Label: "Hermes API key", Secret: true},
	{Key: "HERMES_TIMEOUT_S", Label: "Hermes timeout (s)", Default: "300"},
	{Key: "STT_PROVIDER", Label: "STT provider", Default: "stub", Hint: "stub | deepgram | deepgram-stream | whisper"},
	{Key: "STT_TIMEOUT_S", Label: "STT timeout (s)", Default: "15"},
	{Key: "STT_LOCALE", Label: "STT locale", Default: "id"},
	{Key: "STT_DEEPGRAM_API_KEY", Label: "Deepgram key", Secret: true},
	{Key: "STT_DEEPGRAM_MODEL", Label: "Deepgram model", Default: "nova-3"},
	{Key: "STT_WHISPER_URL", Label: "Whisper server URL", Default: "http://localhost:8000"},
	{Key: "PORT", Label: "Relay port", Default: "8081"},
	{Key: "MQTT_BROKER_URL", Label: "MQTT broker URL", Hint: "empty = log topics to stdout"},
	{Key: "DEVICE_TOKENS", Label: "Device tokens", Secret: true, Hint: "id:token,id:token (empty = open)"},
	{Key: "CHUNK_MAX_COUNT", Label: "Max chunks", Default: "120"},
	{Key: "CHUNK_TTL_S", Label: "Chunk TTL (s)", Default: "300"},
}
