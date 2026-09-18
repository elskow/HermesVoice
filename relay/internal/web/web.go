package web

import (
	"net/http"
)

// Mux assembles the localhost admin surface. Localhost IS the auth boundary
// (no login); main.go binds this mux to 127.0.0.1 only, never the LAN
// listener. Pages register via func values so web never imports feature
// packages (setup imports web for Layout, not vice versa):
//
//	web.Mux(web.Home(homeSummary), setup.NewHandler(envPath).Routes)
//
// Home renders GET / (live summary strip) and the styled 404 for any
// unmatched path. Register it first; feature routes mount after.
func Mux(home HomeSummary, register ...func(*http.ServeMux)) *http.ServeMux {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /{$}", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		_ = Layout("Home", "home", HomeBody(home())).Render(r.Context(), w)
	})
	for _, fn := range register {
		fn(mux)
	}
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.WriteHeader(http.StatusNotFound)
		_ = Layout("Not found", "", NotFoundBody(r.URL.Path)).Render(r.Context(), w)
	})
	return mux
}
