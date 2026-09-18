// Contract doc generator: scans route registrations + errReply call sites
// and emits relay/CONTRACT.md. Run via `go generate ./...` from relay/.
// Drift-proof: the doc is a build artifact of the code, not prose.
package main

import (
	"fmt"
	"go/ast"
	"go/parser"
	"go/token"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
)

type route struct {
	method, path, handler string
}

type xerr struct {
	code, name, msg string
}

func main() {
	fset := token.NewFileSet()
	var routes []route
	var xerrs []xerr

	_, thisFile, _, _ := runtime.Caller(0)
	relayRoot := filepath.Dir(filepath.Dir(filepath.Dir(thisFile)))
	srvDir := filepath.Join(relayRoot, "internal", "server")
	outPath := filepath.Join(relayRoot, "CONTRACT.md")
	files := []string{
		filepath.Join(srvDir, "server.go"),
		filepath.Join(srvDir, "chunks.go"),
	}
	for _, file := range files {
		f, err := parser.ParseFile(fset, file, nil, 0)
		if err != nil {
			panic(err)
		}
		ast.Inspect(f, func(n ast.Node) bool {
			call, ok := n.(*ast.CallExpr)
			if !ok {
				return true
			}
			sel, ok := call.Fun.(*ast.SelectorExpr)
			if !ok {
				return true
			}
			switch sel.Sel.Name {
			case "HandleFunc":
				if len(call.Args) >= 2 {
					if lit, ok := call.Args[0].(*ast.BasicLit); ok {
						parts := strings.SplitN(strings.Trim(lit.Value, `"`), " ", 2)
						handler := ""
						if id, ok := call.Args[1].(*ast.SelectorExpr); ok {
							handler = id.Sel.Name
						}
						routes = append(routes, route{parts[0], parts[1], handler})
					}
				}
			case "errReply":
				if len(call.Args) >= 4 {
					str := func(e ast.Expr) string {
						if lit, ok := e.(*ast.BasicLit); ok {
							return strings.Trim(lit.Value, `"`)
						}
						if sel, ok := e.(*ast.SelectorExpr); ok {
							return sel.Sel.Name
						}
						return "?"
					}
					code := strings.TrimPrefix(str(call.Args[1]), "Status")
					msg := str(call.Args[3])
					if msg == "?" {
						msg = "(dynamic, see handler)"
					}
					xerrs = append(xerrs, xerr{code, str(call.Args[2]), msg})
				}
			}
			return true
		})
	}

	seen := map[string]bool{}
	var out strings.Builder
	out.WriteString("# Relay API contract (generated - do not edit by hand)\n\n")
	out.WriteString("Regenerate: `go generate ./...` from `relay/`. Source of truth is\n")
	out.WriteString("route registrations + `errReply` call sites in `internal/server/`.\n\n")
	out.WriteString("Device speaks no JSON: raw PCM up, text + `X-*` headers down.\n\n")
	out.WriteString("## Endpoints\n\n")
	out.WriteString("| Method | Path | Handler |\n|---|---|---|\n")
	for _, r := range routes {
		fmt.Fprintf(&out, "| %s | `%s` | %s |\n", r.method, r.path, r.handler)
	}
	out.WriteString("\n## X-Error vocabulary\n\n")
	out.WriteString("| HTTP | `X-Error` | Message |\n|---|---|---|\n")
	var names []string
	byName := map[string]xerr{}
	for _, e := range xerrs {
		if !seen[e.name] {
			seen[e.name] = true
			names = append(names, e.name)
			byName[e.name] = e
		}
	}
	sort.Strings(names)
	for _, name := range names {
		e := byName[name]
		fmt.Fprintf(&out, "| %s | `%s` | %s |\n", e.code, e.name, e.msg)
	}
	out.WriteString("\n## Response headers (success)\n\n")
	out.WriteString("`X-Session-ID`, `X-Transcript` (200 chars), `X-STT-MS`, `X-Hermes-MS`,\n")
	out.WriteString("`X-Upload-ID` (chunk start). Request headers: `X-Device-ID` (required),\n")
	out.WriteString("`X-Request-ID` (optional inbound, always echoed back).\n")
	out.WriteString("`Authorization: Bearer <token>` (when `DEVICE_TOKENS` set),\n")
	out.WriteString("`Content-Type: audio/l16;rate=16000;channels=1`.\n")
	if err := os.WriteFile(outPath, []byte(out.String()), 0644); err != nil {
		panic(err)
	}
	fmt.Println("wrote", outPath)
}
