package main

import (
	"context"
	"log"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"hermes-voice/internal/config"
	"hermes-voice/internal/firmware"
	"hermes-voice/internal/fwui"
	"hermes-voice/internal/hermes"
	"hermes-voice/internal/mqtt"
	"hermes-voice/internal/server"
	"hermes-voice/internal/setup"
	"hermes-voice/internal/status"
	"hermes-voice/internal/stt"
	"hermes-voice/internal/web"
)

func main() {
	level := slog.LevelInfo
	switch strings.ToLower(os.Getenv("LOG_LEVEL")) {
	case "debug":
		level = slog.LevelDebug
	case "warn":
		level = slog.LevelWarn
	case "error":
		level = slog.LevelError
	}
	opts := &slog.HandlerOptions{Level: level}
	if os.Getenv("LOG_JSON") != "" {
		slog.SetDefault(slog.New(slog.NewJSONHandler(os.Stdout, opts)))
	} else {
		slog.SetDefault(slog.New(slog.NewTextHandler(os.Stdout, opts)))
	}
	cfg, err := config.Load()
	if err != nil {
		log.Fatalf("config: %v", err)
	}
	sttProv, err := stt.New(cfg)
	if err != nil {
		log.Fatalf("stt: %v", err)
	}
	// Recorder wraps the MQTT sink: last-20 turns + verdict counters feed
	// the status page. Voice flow is untouched; recording is a side effect.
	rec := status.NewRecorder(mqtt.New(cfg.MQTTBrokerURL), 20)
	srv := server.New(cfg, sttProv, hermes.New(cfg), rec, firmware.NewStore(firmware.Dir()))

	// Admin UI: localhost only. Localhost IS the auth boundary (no login);
	// never bind this mux to the LAN listener below.
	setupSrv := &http.Server{Addr: "127.0.0.1:8090", Handler: web.Mux(
		rec.HomeData,
		setup.NewHandler(envPath()).Routes,
		status.NewHandler(rec).Routes,
		fwui.NewHandler(firmware.Dir()).Routes,
	)}
	go func() {
		log.Print("setup UI on http://127.0.0.1:8090/setup")
		if err := setupSrv.ListenAndServe(); err != http.ErrServerClosed {
			log.Printf("setup: %v", err)
		}
	}()

	httpSrv := &http.Server{
		Addr:    ":" + cfg.Port,
		Handler: srv.Handler(),
		// No Read/WriteTimeout: voice uploads + agent turns run minutes.
		// Per-request ctx (HermesTimeout+STTTimeout) governs cancellation.
	}
	go func() {
		log.Printf("hermes-voice relay %s", cfg.Effective())
		if err := httpSrv.ListenAndServe(); err != http.ErrServerClosed {
			log.Fatalf("serve: %v", err)
		}
	}()

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	<-stop
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	httpSrv.Shutdown(ctx)
	srv.Close()
}

// envPath locates relay/.env: REPLAY_ENV wins, else ./relay/.env (repo root
// runs via dev.py / make), else ./.env (direct `go run ./cmd/hermes-voice`).
func envPath() string {
	if v := os.Getenv("RELAY_ENV"); v != "" {
		return v
	}
	for _, p := range []string{"relay/.env", ".env"} {
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}
	return "relay/.env"
}
