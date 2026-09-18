package fwui

import (
	"net/http"
	"net/url"
	"strings"

	"hermes-voice/internal/firmware"
	"hermes-voice/internal/web"
)

// Handler serves the firmware page on localhost only (same listener as
// setup/status via web.Mux). Uploads come from the operator's browser:
// localhost IS the auth boundary, same as the setup UI.
type Handler struct {
	store *firmware.Store
}

func NewHandler(dir string) *Handler { return &Handler{store: firmware.NewStore(dir)} }

func (h *Handler) Routes(mux *http.ServeMux) {
	mux.HandleFunc("GET /firmware", h.show)
	mux.HandleFunc("POST /firmware/publish", h.publish)
}

func (h *Handler) current() (firmware.Manifest, bool) {
	// No manifest yet = empty state, not an error.
	m, err := h.store.Current()
	return m, err == nil
}

func (h *Handler) show(w http.ResponseWriter, r *http.Request) {
	m, have := h.current()
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_ = web.Layout("Firmware", "firmware", page(m, have, r.URL.Query().Get("msg"))).Render(r.Context(), w)
}

func (h *Handler) publish(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseMultipartForm(4<<20 + 1024); err != nil {
		back(w, r, "parse: "+err.Error())
		return
	}
	f, _, err := r.FormFile("image")
	if err != nil {
		back(w, r, "image required (.bin)")
		return
	}
	defer f.Close()
	var devices []string
	for _, d := range strings.Split(r.FormValue("devices"), ",") {
		if d = strings.TrimSpace(d); d != "" {
			devices = append(devices, d)
		}
	}
	m, err := h.store.Publish(r.FormValue("version"), devices, f)
	if err != nil {
		back(w, r, err.Error())
		return
	}
	back(w, r, "published "+m.Version+" ("+m.URL+")")
}

func back(w http.ResponseWriter, r *http.Request, msg string) {
	http.Redirect(w, r, "/firmware?msg="+url.QueryEscape(msg), http.StatusSeeOther)
}
