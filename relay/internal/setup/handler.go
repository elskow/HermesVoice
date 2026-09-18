package setup

import (
	"encoding/json"
	"net/http"
	"net/url"

	"hermes-voice/internal/web"
)

// Handler serves the setup UI on localhost only (see server wiring: the
// route is registered on a 127.0.0.1 listener, never on the LAN bind).
// Localhost IS the auth boundary: no login system, no sessions.
type Handler struct {
	store *Store
}

func NewHandler(envPath string) *Handler {
	return &Handler{store: NewStore(envPath)}
}

func (h *Handler) Routes(mux *http.ServeMux) {
	mux.HandleFunc("GET /setup", h.show)
	mux.HandleFunc("POST /setup/save", h.save)
	mux.HandleFunc("POST /setup/probe", h.probe)
}

func (h *Handler) show(w http.ResponseWriter, r *http.Request) {
	vals := h.store.Load()
	msg := r.URL.Query().Get("msg")
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_ = web.Layout("Relay setup", "setup", page(vals, msg, map[string]ProbeResult{})).Render(r.Context(), w)
}

func (h *Handler) save(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		http.Redirect(w, r, "/setup?msg="+qm("parse: "+err.Error()), http.StatusSeeOther)
		return
	}
	vals := map[string]string{}
	for _, f := range Fields {
		vals[f.Key] = r.FormValue(f.Key)
	}
	if err := h.store.Save(vals); err != nil {
		http.Redirect(w, r, "/setup?msg="+qm(err.Error()), http.StatusSeeOther)
		return
	}
	// Config applies at boot (godotenv): tell the operator to restart.
	// A restart button would need process control; explicit is honest.
	http.Redirect(w, r, "/setup?msg="+qm("saved - restart relay to apply"), http.StatusSeeOther)
}

func qm(s string) string { return url.QueryEscape(s) }

func (h *Handler) probe(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	var res ProbeResult
	switch r.FormValue("provider") {
	case "hermes":
		res = ProbeHermes(r.FormValue("base_url"), r.FormValue("api_key"))
	case "deepgram":
		if r.FormValue("dg_key") == "" {
			res = ProbeResult{"deepgram", false, "no key entered"}
		} else {
			res = ProbeDeepgram(r.FormValue("dg_key"))
		}
	case "whisper":
		res = ProbeWhisper(r.FormValue("whisper_url"))
		if r.FormValue("whisper_url") == "" {
			res = ProbeResult{"whisper", false, "no URL entered"}
		}
	default:
		http.Error(w, "unknown provider", http.StatusBadRequest)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(res)
}
