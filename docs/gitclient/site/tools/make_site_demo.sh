#!/bin/sh
# Builds the workspace photographed for the Git Client website.
# It starts from the standard demo (apps/gitclient/share/demo/make_workspace.sh)
# and rebuilds api-gateway as a small Go service with a real history, so diffs
# and logs read like a working project.
#
# usage: make_site_demo.sh DIR
set -e
HERE=$(cd "$(dirname "$0")" && pwd -P)
REPO=$(cd "$HERE/../../../.." && pwd -P)
ROOT=${1:?usage: make_site_demo.sh DIR}
sh "$REPO/apps/gitclient/share/demo/make_workspace.sh" "$ROOT" >/dev/null
cd "$ROOT"; ROOT=$(pwd -P)
rm -rf work/api-gateway work/api-gateway-hotfix-1287 work/api-gateway-review-412 remotes/api-gateway.git
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
NOW=$(date +%s)
# at NAME HOURS_AGO "message" — commit everything staged as NAME, HOURS_AGO hours back
at() {
  ts=$((NOW - $2 * 3600)); n=$1; e=$(echo "$n" | tr 'A-Z ' 'a-z.')@northwind.dev
  GIT_AUTHOR_NAME="$n" GIT_AUTHOR_EMAIL=$e GIT_COMMITTER_NAME="$n" GIT_COMMITTER_EMAIL=$e \
  GIT_AUTHOR_DATE="$ts +0000" GIT_COMMITTER_DATE="$ts +0000" git commit -q -m "$3"
}
w() { mkdir -p "$(dirname "$1")"; cat > "$1"; git add "$1"; }

git init -q --bare -b main remotes/api-gateway.git
git clone -q remotes/api-gateway.git work/api-gateway 2>/dev/null; cd work/api-gateway; git checkout -q -b main 2>/dev/null || true

w README.md <<'EOF'
# api-gateway

Northwind's edge service: routing, auth and rate limits for every public API.
EOF
w go.mod <<'EOF'
module northwind.dev/api-gateway

go 1.23
EOF
at "Priya Raman" 330 "Initial commit"
w cmd/gateway/main.go <<'EOF'
package main

import (
	"log"
	"net/http"

	"northwind.dev/api-gateway/internal/router"
)

func main() {
	r := router.New()
	log.Println("gateway listening on :8080")
	log.Fatal(http.ListenAndServe(":8080", r))
}
EOF
w internal/router/router.go <<'EOF'
package router

import "net/http"

// New returns the gateway's root handler.
func New() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusNoContent)
	})
	return mux
}
EOF
at "Priya Raman" 300 "Add router and health check"
w internal/auth/oauth.go <<'EOF'
package auth

import "strings"

// Scopes returns the scopes granted by a token's scope claim.
func Scopes(claim string) []string {
	return strings.Split(claim, ",")
}
EOF
at "Sam Ortega" 260 "OAuth: read scopes from token claims"
w internal/limiter/limiter.go <<'EOF'
package limiter

import (
	"sync"
	"time"
)

// Limiter allows a fixed number of requests per key and window.
type Limiter struct {
	mu     sync.Mutex
	limit  int
	window time.Duration
	seen   map[string][]time.Time
}

func New(limit int, window time.Duration) *Limiter {
	return &Limiter{limit: limit, window: window, seen: map[string][]time.Time{}}
}

// Allow reports whether key may make another request now.
func (l *Limiter) Allow(key string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	now := time.Now()
	hits := l.seen[key][:0]
	for _, t := range l.seen[key] {
		if now.Sub(t) < l.window {
			hits = append(hits, t)
		}
	}
	if len(hits) >= l.limit {
		return false
	}
	l.seen[key] = append(hits, now)
	return true
}
EOF
at "Priya Raman" 220 "Rate limiter: sliding window per API key"
w config/local.yaml <<'EOF'
listen: ":8080"
limits:
  per_key: 600
  window: 1m
EOF
w config/staging.yaml <<'EOF'
listen: ":8080"
limits:
  per_key: 1200
  window: 1m
EOF
at "Mei Lin" 190 "Add local and staging configs"
w launch.xml <<'EOF'
<launch project="api-gateway">
    <target name="Gateway" default="true" build="make build" run="bin/gateway --config ${config}">
        <param name="config" choices="config/local.yaml config/staging.yaml" />
    </target>
    <target name="Gateway (race detector)" run="go run -race ./cmd/gateway" />
    <target name="Load test" run="k6 run --vus ${users} load/smoke.js">
        <param name="users" choices="10 100 1000" />
    </target>
    <target name="Docker image" kind="build" run="docker build -t northwind/api-gateway ." />
    <target name="Unit tests" kind="test" run="go test ./..." />
</launch>
EOF
at "Priya Raman" 170 "Add launch targets"
git tag v0.9.0
w internal/router/middleware.go <<'EOF'
package router

import (
	"log/slog"
	"net/http"
	"time"
)

// Logged wraps h with one structured log line per request.
func Logged(h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		h.ServeHTTP(w, r)
		slog.Info("request", "method", r.Method, "path", r.URL.Path, "took", time.Since(start))
	})
}
EOF
at "Mei Lin" 140 "Structured request logging"
git push -q -u origin main 2>/dev/null; git tag v1.0.0; git push -q origin v0.9.0 v1.0.0 2>/dev/null
cat >> internal/router/router.go <<'EOF'

// Version reports the running build.
var Version = "1.0.0"
EOF
git add -A; at "Sam Ortega" 96 "Expose build version"
git push -q origin main 2>/dev/null
w internal/limiter/limiter_test.go <<'EOF'
package limiter

import (
	"testing"
	"time"
)

func TestAllowUpToLimit(t *testing.T) {
	l := New(2, time.Minute)
	if !l.Allow("k") || !l.Allow("k") {
		t.Fatal("first two requests must pass")
	}
	if l.Allow("k") {
		t.Fatal("third request must be rejected")
	}
}
EOF
at "Priya Raman" 30 "Limiter: test the per-key limit"
cat >> internal/router/middleware.go <<'EOF'

// Rejected logs a request refused by the limiter, with its tenant.
func Rejected(r *http.Request, tenant string) {
	slog.Warn("rate limited", "tenant", tenant, "path", r.URL.Path)
}
EOF
git add -A; at "Priya Raman" 26 "Log rejected requests with tenant id"

# uncommitted work: the token bucket rewrite in progress
cat > internal/limiter/limiter.go <<'EOF'
package limiter

import (
	"sync"
	"time"
)

// Limiter is a token bucket per key: rate tokens per second, up to burst.
type Limiter struct {
	mu      sync.Mutex
	rate    float64
	burst   float64
	buckets map[string]*bucket
}

type bucket struct {
	tokens float64
	last   time.Time
}

func New(rate float64, burst int) *Limiter {
	return &Limiter{rate: rate, burst: float64(burst), buckets: map[string]*bucket{}}
}

// Allow reports whether key may make another request now.
func (l *Limiter) Allow(key string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	now := time.Now()
	b, ok := l.buckets[key]
	if !ok {
		b = &bucket{tokens: l.burst, last: now}
		l.buckets[key] = b
	}
	b.tokens = min(l.burst, b.tokens+now.Sub(b.last).Seconds()*l.rate)
	b.last = now
	if b.tokens < 1 {
		return false
	}
	b.tokens--
	return true
}
EOF
git add internal/limiter/limiter.go
cat > internal/limiter/bucket_test.go <<'EOF'
package limiter

import "testing"

func TestBurstRefills(t *testing.T) {
	l := New(10, 3)
	for i := 0; i < 3; i++ {
		if !l.Allow("k") {
			t.Fatalf("request %d inside the burst was rejected", i)
		}
	}
}
EOF
sed -i '' 's/per_key: 600/rate: 10\n  burst: 1000/' config/local.yaml 2>/dev/null || sed -i 's/per_key: 600/rate: 10\n  burst: 1000/' config/local.yaml
perl -0pi -e 's/\tr := router.New\(\)/\tr := router.Logged(router.New())/' cmd/gateway/main.go

git worktree add -q -b hotfix/rate-limit-1287 ../api-gateway-hotfix-1287 origin/main
git worktree add -q -b review/oauth-scopes ../api-gateway-review-412 origin/main
cd ../api-gateway-hotfix-1287
perl -0pi -e 's/if len\(hits\) >= l.limit \{/if len(hits) >= min(l.limit, 1000) {/' internal/limiter/limiter.go; git add -A; at "Priya Raman" 5 "Clamp burst size to 1000"
cd ../api-gateway-review-412
perl -0pi -e 's/strings.Split\(claim, ","\)/strings.Fields(claim) \/\/ RFC 6749 §3.3: space-separated/' internal/auth/oauth.go; git add -A; at "Sam Ortega" 20 "OAuth scopes: parse space-separated claims"
git push -q -u origin review/oauth-scopes 2>/dev/null
cd "$ROOT"
for d in work/*/; do [ -d "${d}.git" ] && echo "$ROOT/${d%/}"; done > paths.txt
{ echo "gitclient-workspace 1"; cat paths.txt; } > workspace.gitworkspace
echo "site demo ready: $ROOT"
