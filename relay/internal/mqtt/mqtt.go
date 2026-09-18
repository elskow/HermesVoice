package mqtt

import (
	"context"
	"encoding/json"
	"fmt"
	"log/slog"
	"net/url"
	"time"

	"github.com/eclipse/paho.golang/autopaho"
	"github.com/eclipse/paho.golang/paho"
)

type Publisher interface {
	PublishState(deviceID, state string)
	PublishPrompt(deviceID, text, sessionID string)
	PublishResponse(deviceID, reply, sessionID string)
	PublishTurn(t Turn)
}

// Turn is one completed device turn: the boundary record. Emitted once per
// turn (success or failure) on node/<id>/turns as JSON, QoS 1 retained=false.
// Any future consumer (dashboard, eval, SQLite archiver) subscribes here;
// the relay never stores turns itself.
type Turn struct {
	DeviceID   string `json:"device_id"`
	SessionID  string `json:"session_id"`
	RequestID  string `json:"request_id,omitempty"`
	Transcript string `json:"transcript"`
	Reply      string `json:"reply,omitempty"`
	Verdict    string `json:"verdict"`
	STTMs      int    `json:"stt_ms"`
	HermesMs   int    `json:"hermes_ms"`
	PCMBytes   int    `json:"pcm_bytes"`
	Ts         int64  `json:"ts"`
}

type LogPublisher struct{}

func (l LogPublisher) PublishState(deviceID, state string) {
	slog.Debug("mqtt state", "device", deviceID, "state", state)
}

func (l LogPublisher) PublishPrompt(deviceID, text, sessionID string) {
	slog.Debug("mqtt prompt", "device", deviceID, "session", sessionID, "text", text)
}

func (l LogPublisher) PublishResponse(deviceID, reply, sessionID string) {
	slog.Debug("mqtt response", "device", deviceID, "session", sessionID, "reply", reply)
}

func (l LogPublisher) PublishTurn(t Turn) {
	t.Ts = time.Now().Unix()
	body, _ := json.Marshal(t)
	slog.Info("mqtt turn", "device", t.DeviceID, "verdict", t.Verdict, "turn", string(body))
}

type client struct {
	cm    *autopaho.ConnectionManager
	queue chan queuedMsg
}

// paho.golang autopaho: persistent ctx (not timeout-scoped) keeps the conn
// alive; AwaitConnection gates only boot so publishes never race connect.
// State is retained QoS 1, prompt/response QoS 0 fire-and-forget.
func New(brokerURL string) Publisher {
	if brokerURL == "" {
		slog.Info("mqtt sink", "mode", "log", "reason", "no broker configured")
		return LogPublisher{}
	}
	u, err := url.Parse(brokerURL)
	if err != nil {
		slog.Warn("mqtt sink", "mode", "log", "reason", "bad broker url", "url", brokerURL, "err", err)
		return LogPublisher{}
	}
	cm, err := autopaho.NewConnection(context.Background(), autopaho.ClientConfig{
		ServerUrls:                    []*url.URL{u},
		KeepAlive:                     20,
		CleanStartOnInitialConnection: false,
		SessionExpiryInterval:         60,
		OnConnectionUp: func(*autopaho.ConnectionManager, *paho.Connack) {
			slog.Info("mqtt connected", "broker", u.Redacted())
		},
		OnConnectError: func(err error) { slog.Warn("mqtt connect error", "err", err) },
		ClientConfig: paho.ClientConfig{
			ClientID:      "hermes-voice",
			OnClientError: func(err error) { slog.Warn("mqtt client error", "err", err) },
			OnServerDisconnect: func(d *paho.Disconnect) {
				slog.Warn("mqtt server disconnect", "reason", d.ReasonCode)
			},
		},
	})
	if err != nil {
		slog.Warn("mqtt sink", "mode", "log", "reason", "connect failed, voice unaffected", "err", err)
		return LogPublisher{}
	}
	boot, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := cm.AwaitConnection(boot); err != nil {
		slog.Warn("mqtt sink", "mode", "log", "reason", "broker unreachable, voice unaffected", "err", err)
		return LogPublisher{}
	}
	slog.Info("mqtt sink", "mode", "broker", "broker", u.Redacted())
	c := &client{cm: cm, queue: make(chan queuedMsg, 64)}
	go c.loop()
	return c
}

type queuedMsg struct {
	topic   string
	payload []byte
	retain  bool
}

func (c *client) loop() {
	for m := range c.queue {
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		qos := byte(0)
		if m.retain {
			qos = 1
		}
		_, err := c.cm.Publish(ctx, &paho.Publish{
			QoS:     qos,
			Topic:   m.topic,
			Payload: m.payload,
			Retain:  m.retain,
		})
		cancel()
		if err != nil {
			slog.Error("mqtt publish failed", "topic", m.topic, "err", err)
		}
	}
}

func (c *client) pub(topic string, payload []byte, retain bool) {
	select {
	case c.queue <- queuedMsg{topic, payload, retain}:
	default:
		slog.Warn("mqtt dropped", "topic", topic, "reason", "queue full")
	}
}

func (c *client) PublishState(deviceID, state string) {
	c.pub(fmt.Sprintf("node/%s/state", deviceID), []byte(state), true)
}

func (c *client) PublishPrompt(deviceID, text, sessionID string) {
	body, _ := json.Marshal(map[string]any{"text": text, "session_id": sessionID, "ts": time.Now().Unix()})
	c.pub(fmt.Sprintf("node/%s/prompt", deviceID), body, false)
}

func (c *client) PublishResponse(deviceID, reply, sessionID string) {
	body, _ := json.Marshal(map[string]any{"reply": reply, "session_id": sessionID, "ts": time.Now().Unix()})
	c.pub(fmt.Sprintf("node/%s/response", deviceID), body, false)
}

func (c *client) PublishTurn(t Turn) {
	if t.Ts == 0 {
		t.Ts = time.Now().Unix()
	}
	body, _ := json.Marshal(t)
	c.pub(fmt.Sprintf("node/%s/turns", t.DeviceID), body, false)
}
