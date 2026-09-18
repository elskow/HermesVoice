package web

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func fakeSummary() HomeData {
	return HomeData{StartedAt: time.Now().Add(-time.Hour), Uptime: time.Hour, Total: 3, OK: 2}
}

func TestHomeShowsSummary(t *testing.T) {
	mux := Mux(fakeSummary)
	req := httptest.NewRequest("GET", "/", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)
	if rec.Code != 200 {
		t.Fatalf("code = %d, want 200", rec.Code)
	}
	body := rec.Body.String()
	for _, want := range []string{"Hermes Voice", "2 ok of 3 turns", `href="/setup"`, `href="/status"`} {
		if !contains(body, want) {
			t.Fatalf("home missing %q", want)
		}
	}
}

func TestNotFoundIsStyled(t *testing.T) {
	mux := Mux(fakeSummary, func(m *http.ServeMux) {
		m.HandleFunc("GET /x", func(w http.ResponseWriter, _ *http.Request) {})
	})
	req := httptest.NewRequest("GET", "/nope", nil)
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)
	if rec.Code != 404 {
		t.Fatalf("code = %d, want 404", rec.Code)
	}
	body := rec.Body.String()
	for _, want := range []string{"Page not found", "/nope", "Back home", `class="top"`} {
		if !contains(body, want) {
			t.Fatalf("404 missing %q", want)
		}
	}
}

func contains(s, sub string) bool { return strings.Contains(s, sub) }
