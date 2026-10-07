#!/bin/sh
# Builds the demo workspace used for gitclient overview screenshots and manual testing.
#
# Persona: Priya Raman, platform engineer at Northwind. Six services, two of them with
# extra worktrees (a hotfix and a PR review). Every morning she needs to know what she
# left uncommitted or unpushed yesterday, and what her teammates pushed overnight.
#
# usage: make_workspace.sh DIR
#        gitclient DIR/workspace.gitworkspace
#        gitclient $(cat DIR/paths.txt)
set -e
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE
ROOT=${1:?usage: make_workspace.sh DIR}
rm -rf "$ROOT"; mkdir -p "$ROOT/remotes" "$ROOT/work"; cd "$ROOT"; ROOT=$(pwd -P)
export GIT_AUTHOR_NAME="Priya Raman" GIT_AUTHOR_EMAIL=priya@northwind.dev
export GIT_COMMITTER_NAME="Priya Raman" GIT_COMMITTER_EMAIL=priya@northwind.dev
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
NOW=$(date +%s)

# commit "message" hours_ago [file] — stage a small change to FILE and commit it back in time.
commit() {
  f=${3:-src/app.txt}; mkdir -p "$(dirname "$f")"; echo "$1 $RANDOM" >> "$f"; git add "$f"
  ts=$((NOW - $2 * 3600)); GIT_AUTHOR_DATE="$ts +0000" GIT_COMMITTER_DATE="$ts +0000" git commit -q -m "$1"
}
# new_repo NAME — bare remote plus a working clone with two published commits on main.
new_repo() {
  git init -q --bare -b main "remotes/$1.git"; git clone -q "remotes/$1.git" "work/$1" 2>/dev/null; cd "work/$1"
  git checkout -q -b main 2>/dev/null || true
  commit "Initial commit" 240 README.md; commit "Add project skeleton" 200; git push -q -u origin main 2>/dev/null; cd "$ROOT"
}
# teammate NAME "message" hours_ago — push a commit from a second clone (makes the main clone "behind").
teammate() {
  rm -rf "$ROOT/tmp_mate"; git clone -q "remotes/$1.git" "$ROOT/tmp_mate"; cd "$ROOT/tmp_mate"
  GIT_AUTHOR_NAME="Sam Ortega" GIT_COMMITTER_NAME="Sam Ortega" commit "$2" "$3" ${4:-src/mate.txt}; git push -q origin main; cd "$ROOT"; rm -rf "$ROOT/tmp_mate"
}
edit() { for f in "$@"; do mkdir -p "$(dirname "$f")"; echo "wip $RANDOM" >> "$f"; done; }

# api-gateway: dirty main with unpushed work, plus two worktrees
new_repo api-gateway; cd work/api-gateway
commit "Rate limiter: token bucket per API key" 30 src/limiter.txt; commit "Log rejected requests with tenant id" 26 src/log.txt
edit src/limiter.txt src/router.txt src/config.txt; echo new > src/limiter_test.txt; git stash list >/dev/null
git worktree add -q -b hotfix/rate-limit-1287 ../api-gateway-hotfix-1287 origin/main
git worktree add -q -b review/oauth-scopes ../api-gateway-review-412 origin/main
cd ../api-gateway-hotfix-1287; commit "Clamp burst size to 1000" 5 src/limiter.txt; edit src/limiter.txt src/router.txt; git add src/router.txt
cd ../api-gateway-review-412; commit "OAuth scopes: parse space-separated claims" 20 src/oauth.txt; git push -q -u origin review/oauth-scopes 2>/dev/null; cd "$ROOT"

# billing-service: merge conflict in flight
new_repo billing-service; cd work/billing-service
git checkout -q -b release/2026.10; commit "Round invoice totals half-even" 50 src/invoice.txt; git push -q -u origin release/2026.10 2>/dev/null
git checkout -q main; commit "Invoice totals: use decimal type" 44 src/invoice.txt; git push -q origin main
git checkout -q release/2026.10; git merge main >/dev/null 2>&1 || true; edit src/tax.txt; cd "$ROOT"

# web-console: clean but four commits behind
new_repo web-console; teammate web-console "Refactor sidebar navigation" 14 src/nav.txt; teammate web-console "Fix focus ring on dark theme" 12 src/theme.txt
teammate web-console "Bump vite to 6.2" 9 package.txt; teammate web-console "Add empty state to audit log" 3 src/audit.txt; cd work/web-console && git fetch -q && cd "$ROOT"

# infra-terraform: unpublished feature branch with new files
new_repo infra-terraform; cd work/infra-terraform; git checkout -q -b feature/eu-west-2
commit "Add eu-west-2 VPC module" 22 modules/vpc.txt; commit "Peer eu-west-2 with shared services" 21 modules/peering.txt; commit "Enable flow logs" 20 modules/logs.txt
echo a > modules/nat.txt; echo b > modules/outputs.txt; cd "$ROOT"

# data-pipeline: heavy uncommitted work, some staged
new_repo data-pipeline; cd work/data-pipeline
for n in ingest clean join export schedule retry metrics; do commit "Add $n stage" 100 "jobs/$n.txt"; done; git push -q origin main 2>/dev/null
edit jobs/ingest.txt jobs/clean.txt jobs/join.txt jobs/export.txt jobs/schedule.txt jobs/retry.txt jobs/metrics.txt; git add jobs/ingest.txt jobs/clean.txt jobs/join.txt jobs/export.txt; cd "$ROOT"

# mobile-app: one unpushed commit and two stashes
new_repo mobile-app; cd work/mobile-app
commit "Fix crash when offline on cold start" 8 src/boot.txt
edit src/a.txt; git add src/a.txt; git stash -q; edit src/b.txt; git add src/b.txt; git stash -q; cd "$ROOT"

# docs-site and design-tokens: nothing to do
new_repo docs-site; new_repo design-tokens
cd work/design-tokens; commit "Tokens: add elevation scale" 70 tokens/elevation.txt; git push -q origin main; cd "$ROOT"

for d in work/*/; do [ -d "${d}.git" ] && echo "$ROOT/${d%/}"; done > paths.txt
{ echo "gitclient-workspace 1"; cat paths.txt; } > workspace.gitworkspace
echo "workspace ready: $ROOT ($(wc -l < paths.txt | tr -d ' ') repositories)"
echo "open with: gitclient $ROOT/workspace.gitworkspace"
