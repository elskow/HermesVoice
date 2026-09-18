package status

import (
	"sync"
	"time"

	"hermes-voice/internal/mqtt"
	"hermes-voice/internal/web"
)

// Snapshot is one point-in-time view of relay health. Everything here is
// observable state, never secrets: uptime, per-verdict counters, last turns.
type Snapshot struct {
	StartedAt time.Time
	Uptime    time.Duration
	Verdicts  map[string]int
	Recent    []mqtt.Turn
	Cap       int
}

// Recorder keeps the last N turns plus per-verdict counters. It wraps a
// downstream Publisher: voice flow is untouched, recording is a side effect.
type Recorder struct {
	mu       sync.Mutex
	down     mqtt.Publisher
	started  time.Time
	verdicts map[string]int
	recent   []mqtt.Turn
	cap      int
}

func NewRecorder(down mqtt.Publisher, cap int) *Recorder {
	if cap <= 0 {
		cap = 20
	}
	return &Recorder{down: down, started: time.Now(), verdicts: map[string]int{}, cap: cap}
}

// HomeData feeds the admin home summary strip without exposing web to
// status internals: counters only, no turns.
func (r *Recorder) HomeData() web.HomeData {
	r.mu.Lock()
	defer r.mu.Unlock()
	total := 0
	for _, v := range r.verdicts {
		total += v
	}
	return web.HomeData{StartedAt: r.started, Uptime: time.Since(r.started), Total: total, OK: r.verdicts["ok"]}
}

func (r *Recorder) Snapshot() Snapshot {
	r.mu.Lock()
	defer r.mu.Unlock()
	recent := make([]mqtt.Turn, len(r.recent))
	copy(recent, r.recent)
	verdicts := make(map[string]int, len(r.verdicts))
	for k, v := range r.verdicts {
		verdicts[k] = v
	}
	return Snapshot{StartedAt: r.started, Uptime: time.Since(r.started), Verdicts: verdicts, Recent: recent, Cap: r.cap}
}

func (r *Recorder) PublishState(deviceID, state string) { r.down.PublishState(deviceID, state) }

func (r *Recorder) PublishPrompt(deviceID, text, sessionID string) {
	r.down.PublishPrompt(deviceID, text, sessionID)
}

func (r *Recorder) PublishResponse(deviceID, reply, sessionID string) {
	r.down.PublishResponse(deviceID, reply, sessionID)
}

func (r *Recorder) PublishTurn(t mqtt.Turn) {
	r.down.PublishTurn(t)
	r.mu.Lock()
	defer r.mu.Unlock()
	r.verdicts[t.Verdict]++
	r.recent = append([]mqtt.Turn{t}, r.recent...)
	if len(r.recent) > r.cap {
		r.recent = r.recent[:r.cap]
	}
}
