# Terminal Vision

> Agent Development Environment (ADE) focused on visibility and control of parallel agents, worktrees, and PR status.

## What we want

Stop losing track of what agents did.

Today with Claude Code, Codex, Cursor Agent etc. you get:
- a terminal that is running or finished
- sometimes a PR icon / link

You do **not** get a clear answer to:
- which worktree/branch is this?
- is the PR open, needs review, has conflicts, or already merged?
- which agent is still working vs. waiting for approval vs. done?
- how do I jump back to the exact session that produced this change?

The goal of `apps/terminal` (together with `apps/gitclient`) is to make this obvious.

## Core idea

**Every tab/pane is a terminal.**  
Inside it you can run anything that works in a PTY:
- coding agents (Claude Code, Codex, OpenCode, Aider, …)
- editors (nvim, vim, helix, …)
- git tools (lazygit, gh)
- builds, tests, logs

On top of that we track:
- which repository and **worktree** the pane belongs to
- agent status (idle / running / needs approval / failed / done)
- branch / PR status (open, merged, conflicts, behind, needs rebase)
- quick jump from overview → terminal session → diff

This is closer to **Superset**, **Conductor**, and **Orca** than to a plain terminal emulator or a classic Git GUI.

## Key references

- [Superset](https://superset.sh/) — parallel agents, worktrees, changes pane, YC, strong traction
- [Conductor](https://www.conductor.build/) — isolated workspaces per agent, status in sidebar, Series A
- [Orca](https://onorca.dev/) — open-source ADE, worktree-per-agent, high GitHub stars
- [BridgeMind / BridgeSpace](https://www.bridgemind.ai/) — agent workspace + kanban + terminals

## What success looks like

1. Open a workspace that shows all active agent sessions + their worktrees.
2. Instantly see which ones are merged, which need review, which have conflicts.
3. Click a session → land in the terminal (or open nvim in it).
4. Review the diff without leaving the app.
5. Persist the layout and running processes so you can come back later.

## Relationship to other Orion apps

- `apps/terminal` — the place where agents and editors run (PTY-centric).
- `apps/gitclient` — repository overview, branch/PR status, worktree cards.
- Together they form a lightweight ADE instead of competing as “yet another Git client” or “yet another terminal”.

## Non-goals (for now)

- Building our own full code editor from scratch.
- Reimplementing agent logic — we host existing CLI agents.
- Competing purely on terminal emulation quality with Warp/WezTerm.

## Next practical steps

- Surface worktree + branch + PR status next to each terminal pane / in a sidebar.
- Link terminal sessions to gitclient overview cards.
- Persistent workspaces (repos, worktrees, layout, agent commands).
- Basic agent status detection (running / finished / needs input) from the PTY or simple hooks.

---

This is the direction that matches the current hot ADE tools and the pain of “I don’t know what got merged”.
