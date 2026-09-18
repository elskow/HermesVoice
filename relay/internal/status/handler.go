package status

import (
	"net/http"

	"hermes-voice/internal/web"
)

// Handler serves the status page on localhost only (same listener as setup;
// see web.Mux wiring in main.go). Read-only: no writes, no secrets.
type Handler struct {
	rec *Recorder
}

func NewHandler(rec *Recorder) *Handler { return &Handler{rec: rec} }

func (h *Handler) Routes(mux *http.ServeMux) {
	mux.HandleFunc("GET /status", h.show)
}

func (h *Handler) show(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_ = web.Layout("Status", "status", page(h.rec.Snapshot())).Render(r.Context(), w)
}
