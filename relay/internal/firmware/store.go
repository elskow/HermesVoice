package firmware

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
)

// Store serves version + binaries from a directory on disk. Layout:
//
//	firmware/
//	  manifest.json            {"version","url","sha256","devices?"}
//	  voice-node-<version>.bin
//
// The device GETs /v1/firmware/version?device_id=... (200 + manifest, or
// 204 when nothing eligible), then GETs the url (same relay, plain path).
// SHA256 is computed at upload, verified by the device after download.
type Store struct {
	dir string
}

func NewStore(dir string) *Store { return &Store{dir: dir} }

// Dir locates the on-disk image store: FIRMWARE_DIR wins, else
// relay/firmware (repo root runs), else ./firmware (relay-dir runs).
func Dir() string {
	if v := os.Getenv("FIRMWARE_DIR"); v != "" {
		return v
	}
	for _, p := range []string{"relay/firmware", "firmware"} {
		if st, err := os.Stat(p); err == nil && st.IsDir() {
			return p
		}
	}
	return "relay/firmware"
}

// Current returns the published manifest, or an error when nothing is
// published yet (the UI renders that as empty state, not failure).
func (s *Store) Current() (Manifest, error) { return s.manifest() }

func (s *Store) manifest() (Manifest, error) {
	var m Manifest
	data, err := os.ReadFile(filepath.Join(s.dir, "manifest.json"))
	if err != nil {
		return m, err
	}
	if err := json.Unmarshal(data, &m); err != nil {
		return m, err
	}
	if m.Version == "" || m.URL == "" || m.SHA256 == "" {
		return m, fmt.Errorf("manifest missing version/url/sha256")
	}
	return m, nil
}

// HandleVersion answers the device poll. 204 = no update eligible (device
// keeps running; not an error, not a turn).
func (s *Store) HandleVersion(w http.ResponseWriter, r *http.Request) {
	m, err := s.manifest()
	if err != nil {
		http.Error(w, "no firmware published", http.StatusNotFound)
		return
	}
	if !m.Eligible(r.URL.Query().Get("device_id")) {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(m)
}

// HandleBinary serves the image. Path is basename-locked: no traversal
// (the URL comes from our own manifest, but defense costs one call).
func (s *Store) HandleBinary(w http.ResponseWriter, r *http.Request) {
	name := filepath.Base(r.PathValue("name"))
	if name == "" || strings.Contains(name, "..") || !strings.HasSuffix(name, ".bin") {
		http.Error(w, "bad name", http.StatusBadRequest)
		return
	}
	http.ServeFile(w, r, filepath.Join(s.dir, name))
}

// Publish stores an uploaded image, hashes it, and rewrites the manifest
// atomically (temp + rename: readers never see a half manifest).
func (s *Store) Publish(version string, devices []string, body io.Reader) (Manifest, error) {
	var m Manifest
	if version == "" {
		return m, fmt.Errorf("version required")
	}
	if err := os.MkdirAll(s.dir, 0755); err != nil {
		return m, err
	}
	name := "voice-node-" + version + ".bin"
	tmp, err := os.CreateTemp(s.dir, "upload-*")
	if err != nil {
		return m, err
	}
	h := sha256.New()
	n, err := io.Copy(io.MultiWriter(tmp, h), io.LimitReader(body, 4<<20))
	tmp.Close()
	if err != nil {
		os.Remove(tmp.Name())
		return m, err
	}
	if n == 0 {
		os.Remove(tmp.Name())
		return m, fmt.Errorf("empty image")
	}
	if err := os.Rename(tmp.Name(), filepath.Join(s.dir, name)); err != nil {
		return m, err
	}
	m = Manifest{Version: version, URL: "/v1/firmware/" + name,
		SHA256: hex.EncodeToString(h.Sum(nil)), Devices: devices}
	data, _ := json.MarshalIndent(m, "", "  ")
	mtmp, err := os.CreateTemp(s.dir, "manifest-*")
	if err != nil {
		return m, err
	}
	if _, err := mtmp.Write(data); err != nil {
		mtmp.Close()
		os.Remove(mtmp.Name())
		return m, err
	}
	mtmp.Close()
	if err := os.Rename(mtmp.Name(), filepath.Join(s.dir, "manifest.json")); err != nil {
		return m, err
	}
	return m, nil
}
