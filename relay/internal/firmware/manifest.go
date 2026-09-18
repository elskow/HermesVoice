package firmware

// Manifest is the version truth the relay serves. One file on disk
// (firmware/manifest.json next to the binaries), hand- or UI-written.
// Rollout is a per-device allowlist: empty means everyone eligible.
// No database: the relay serves files, devices decide.
type Manifest struct {
	Version string   `json:"version"`
	URL     string   `json:"url"`
	SHA256  string   `json:"sha256"`
	Devices []string `json:"devices,omitempty"`
}

// Eligible reports whether deviceID may take this manifest. Empty
// allowlist = fleet-wide rollout; otherwise only listed devices.
func (m Manifest) Eligible(deviceID string) bool {
	if len(m.Devices) == 0 {
		return true
	}
	for _, d := range m.Devices {
		if d == deviceID {
			return true
		}
	}
	return false
}
