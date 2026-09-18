// Virtual ESP32 PTT device: replays the firmware upload path byte-identically
// so relay devs never need silicon. Go port of tools/virt-device/virt.py
// (kept as the behavioral spec); black-box over HTTP, like the device.
package main

import (
	"bytes"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// ChunkBytes mirrors NODE_VOICE_CHUNK_BYTES in firmware. One Go-side source;
// keep in sync on firmware change (CI could assert via clips size one day).
const ChunkBytes = 32768

const ctype = "audio/l16;rate=16000;channels=1"

var bearerToken string

func doPost(client *http.Client, url, device string, pcm []byte) (int, http.Header, []byte) {
	var body io.Reader
	if pcm != nil {
		body = bytes.NewReader(pcm)
	}
	req, err := http.NewRequest("POST", url, body)
	if err != nil {
		return 0, nil, []byte(err.Error())
	}
	req.Header.Set("X-Device-ID", device)
	if bearerToken != "" {
		req.Header.Set("Authorization", "Bearer "+bearerToken)
	}
	if pcm != nil {
		req.Header.Set("Content-Type", ctype)
	} else {
		req.Header.Set("Content-Length", "0")
	}
	resp, err := client.Do(req)
	if err != nil {
		return 0, nil, []byte(err.Error())
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(resp.Body)
	return resp.StatusCode, resp.Header, b
}

func singleShot(client *http.Client, relay, device string, pcm []byte) (int, http.Header, []byte) {
	return doPost(client, relay+"/v1/voice", device, pcm)
}

func chunked(client *http.Client, relay, device string, pcm []byte, dropSeq, paceMs int, cancelMid bool) (int, http.Header, []byte) {
	status, headers, _ := doPost(client, relay+"/v1/chunks/start", device, nil)
	if status != 200 {
		return status, headers, []byte("start failed")
	}
	upl := headers.Get("X-Upload-ID")
	seq := 0
	for off := 0; off < len(pcm); off += ChunkBytes {
		if dropSeq >= 0 && seq == dropSeq {
			seq++
			continue
		}
		if cancelMid && seq == 1 {
			doPost(client, relay+"/v1/cancel?device_id="+device, device, nil)
			// Continue to finish: relay dropped the session, so finish
			// must come back unknown_upload (404). That IS the assertion.
			break
		}
		end := off + ChunkBytes
		if end > len(pcm) {
			end = len(pcm)
		}
		if paceMs > 0 {
			time.Sleep(time.Duration(paceMs) * time.Millisecond)
		}
		status, headers, body := doPost(client,
			fmt.Sprintf("%s/v1/chunks/%s?seq=%d", relay, upl, seq), device, pcm[off:end])
		if status != 200 {
			return status, headers, body
		}
		seq++
	}
	return doPost(client, relay+"/v1/chunks/"+upl+"/finish", device, nil)
}

type runOpts struct {
	clip, device, relayURL, token, expect string
	dropSeq, paceMs                       int
	cancelMid                             bool
}

func runOnce(client *http.Client, o runOpts) (status int, xerr string, transcript string, body []byte, secs float64) {
	bearerToken = o.token
	pcm, err := os.ReadFile(o.clip)
	if err != nil {
		return 0, "read-fail", "", []byte(err.Error()), 0
	}
	if len(pcm) == 0 {
		return 2, "", "", []byte("empty clip"), 0
	}
	t0 := time.Now()
	var headers http.Header
	if len(pcm) <= ChunkBytes && o.dropSeq < 0 && !o.cancelMid {
		status, headers, body = singleShot(client, o.relayURL, o.device, pcm)
	} else {
		status, headers, body = chunked(client, o.relayURL, o.device, pcm, o.dropSeq, o.paceMs, o.cancelMid)
	}
	secs = time.Since(t0).Seconds()
	if headers != nil {
		xerr = headers.Get("X-Error")
		transcript = headers.Get("X-Transcript")
	}
	return status, xerr, transcript, body, secs
}

func checkWant(xerr string, status int, expect string) bool {
	want := ""
	if expect != "ok" {
		want = expect
	}
	return xerr == want && (want != "" || status == 200)
}

// splitScenarioFlag parses "path,offline": offline runs only scenarios
// whose expect != ok (gap/cancel paths never reach Hermes).
func splitScenarioFlag(s string) (string, bool) {
	if strings.HasSuffix(s, ",offline") {
		return strings.TrimSuffix(s, ",offline"), true
	}
	return s, false
}

func runScenarioFile(client *http.Client, path, relayURL, device, token string, onlyOffline bool) int {
	data, err := os.ReadFile(path)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		return 2
	}
	fails := 0
	n := 0
	for _, line := range strings.Split(string(data), "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		parts := strings.Split(line, "|")
		if len(parts) != 4 {
			fmt.Printf("SKIP bad line: %s\n", line)
			continue
		}
		name := strings.TrimSpace(parts[0])
		clip := strings.TrimSpace(parts[1])
		if !filepath.IsAbs(clip) {
			clip = filepath.Join(filepath.Dir(path), clip)
		}
		flagStr := strings.TrimSpace(parts[2])
		expect := strings.TrimSpace(parts[3])
		dropSeq := -1
		paceMs := 0
		cancelMid := false
		for _, f := range strings.Fields(flagStr) {
			if v, ok := strings.CutPrefix(f, "--drop-seq="); ok {
				fmt.Sscanf(v, "%d", &dropSeq)
			} else if v, ok := strings.CutPrefix(f, "--drop-seq "); ok {
				fmt.Sscanf(strings.TrimSpace(v), "%d", &dropSeq)
			} else if f == "--cancel-mid" {
				cancelMid = true
			}
		}
		// bare "--drop-seq 2" (space form) fallback
		if fs := strings.Fields(flagStr); len(fs) == 2 && fs[0] == "--drop-seq" {
			fmt.Sscanf(fs[1], "%d", &dropSeq)
		}
		if onlyOffline && expect == "ok" {
			fmt.Printf("[skip] %-12s needs live Hermes\n", name)
			continue
		}
		n++
		status, xerr, _, _, secs := runOnce(client, runOpts{
			clip: clip, device: device, relayURL: relayURL,
			token: token, dropSeq: dropSeq, paceMs: paceMs, cancelMid: cancelMid,
		})
		ok := checkWant(xerr, status, expect)
		if !ok {
			fails++
		}
		mark := "ok"
		if !ok {
			mark = "MISMATCH"
		}
		fmt.Printf("[%s] %-12s http=%d x-error=%s %.1fs (want %s)\n",
			mark, name, status, orDash(xerr), secs, expect)
	}
	fmt.Printf("%d/%d scenarios passed\n", n-fails, n)
	if fails > 0 {
		return 1
	}
	return 0
}

func main() {
	clip := flag.String("clip", "", "16kHz mono S16LE PCM file")
	device := flag.String("device", "node-sim", "X-Device-ID value")
	relayURL := flag.String("relay", "http://127.0.0.1:8081", "relay base URL")
	dropSeq := flag.Int("drop-seq", -1, "skip chunk seq (gap -> upload_gap)")
	cancelMid := flag.Bool("cancel-mid", false, "cancel mid-upload")
	expect := flag.String("expect", "", "assert X-Error equals this (or 'ok')")
	token := flag.String("token", "", "Bearer device token (DEVICE_TOKENS)")
	paceMs := flag.Int("pace-ms", 0, "sleep between chunks (race tests)")
	scenario := flag.String("scenario", "", "run scenarios file (or 'all' for testdata/scenarios.txt)")
	flag.Parse()

	client := &http.Client{Timeout: 120 * time.Second}
	if *scenario != "" {
		onlyOffline := false
		path := *scenario
		if rest, ok := splitScenarioFlag(*scenario); ok {
			path, onlyOffline = rest, true
		}
		if path == "all" {
			path = "testdata/scenarios.txt"
			if _, err := os.Stat(path); err != nil {
				if ex, err := os.Executable(); err == nil {
					alt := filepath.Join(filepath.Dir(ex), "scenarios.txt")
					if _, err := os.Stat(alt); err == nil {
						path = alt
					}
				}
			}
		}
		os.Exit(runScenarioFile(client, path, *relayURL, *device, *token, onlyOffline))
	}
	if *clip == "" {
		fmt.Fprintln(os.Stderr, "missing --clip (or use --scenario all)")
		os.Exit(2)
	}
	status, xerr, transcript, body, secs := runOnce(client, runOpts{
		clip: *clip, device: *device, relayURL: *relayURL, token: *token,
		expect: *expect, dropSeq: *dropSeq, paceMs: *paceMs, cancelMid: *cancelMid,
	})
	fmt.Printf("http=%d x-error=%s %.1fs\n", status, orDash(xerr), secs)
	fmt.Printf("transcript=%s\n", orDash(transcript))
	fmt.Printf("reply: %.500s\n", body)
	if *expect != "" && !checkWant(xerr, status, *expect) {
		want := *expect
		if want == "ok" {
			want = "(none)"
		}
		fmt.Printf("MISMATCH: expected x-error=%s\n", want)
		os.Exit(1)
	}
}

func orDash(s string) string {
	if s == "" {
		return "-"
	}
	return s
}
