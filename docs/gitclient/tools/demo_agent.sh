#!/bin/sh
# Stand-in for `codex` and `claude` while photographing the Agents page.
# Link it into a directory on PATH under both names; Git Client starts it in
# the task's worktree with the prompt as the last argument. It prints sample
# progress, makes the edit its task names in the site demo's api-gateway, and
# exits. The request-metrics task keeps working so the list has a task in
# progress.
#
# usage: ln -s demo_agent.sh DIR/codex; ln -s demo_agent.sh DIR/claude
for arg do prompt=$arg; done
name=${0##*/}
g() { printf '\033[32m%s\033[0m %s \033[32m%s\033[0m\n' "$1" "$2" "$3"; }
step() { sleep 0.6; printf '\n\033[2mthinking\033[0m  %s\n' "$1"; }
printf '\033[36m>_ %s exec\033[0m · demo\n' "$name"

case "$prompt" in
  Rate*)
    step "the window check runs time.Since per hit; compute the cutoff once"
    perl -0pi -e 's/\thits := l.seen\[key\]\[:0\]/\tcutoff := now.Add(-l.window)\n\thits := l.seen[key][:0]/; s/if now.Sub\(t\) < l.window \{/if t.After(cutoff) {/' internal/limiter/limiter.go
    cat >> internal/limiter/limiter_test.go <<'EOF'

func TestWindowExpires(t *testing.T) {
	l := New(1, 10*time.Millisecond)
	if !l.Allow("k") {
		t.Fatal("first request must pass")
	}
	time.Sleep(15 * time.Millisecond)
	if !l.Allow("k") {
		t.Fatal("a request after the window must pass")
	}
}
EOF
    g apply_patch internal/limiter/limiter.go "+2 -1"
    g apply_patch internal/limiter/limiter_test.go "+11"
    sleep 0.6; g exec "go test ./internal/limiter" ok
    printf '\nThe limiter computes its cutoff once per call; a test covers window expiry.\n' ;;
  OAuth*)
    step "scopes are comma-split; RFC 6749 §3.3 says space-delimited"
    perl -0pi -e 's/strings.Split\(claim, ","\)/strings.Fields(claim)/' internal/auth/oauth.go
    cat > internal/auth/oauth_test.go <<'EOF'
package auth

import "testing"

func TestScopesAreSpaceSeparated(t *testing.T) {
	got := Scopes("read:orders  write:orders")
	if len(got) != 2 || got[0] != "read:orders" || got[1] != "write:orders" {
		t.Fatalf("Scopes = %q", got)
	}
}
EOF
    g apply_patch internal/auth/oauth.go "+1 -1"
    g apply_patch internal/auth/oauth_test.go "+10"
    sleep 0.6; g exec "go test ./internal/auth" ok
    printf '\nScopes now follow RFC 6749: split on whitespace, with a regression test.\n' ;;
  *)
    step "add a counter per route and status in the logging middleware"
    g apply_patch internal/router/metrics.go "+28"
    while :; do sleep 2; printf '\033[2mworking\033[0m  wiring /metrics into the router\n'; done ;;
esac
