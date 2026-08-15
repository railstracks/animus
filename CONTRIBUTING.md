# Contributing to Animus

This guide is for both human contributors and AI agents working on Animus. Whether you're typing commands or using the `gh` CLI, the workflow is the same.

## Branch Model

| Branch | Purpose |
|--------|---------|
| `main` | Release-only. Merged from `dev` on tag. Never commit directly. |
| `dev` | Integration branch. All PRs target `dev`. |
| `ticket/NNN-short-desc` | One branch per issue. Deleted after merge. |
| `fix/short-desc` | For bug fixes without a numbered issue. |

**Rules:**
- Never commit directly to `main` or `dev`. Always work on a ticket/fix branch and open a PR.
- One branch per issue. If an issue spans multiple PRs, use `ticket/NNN-1-desc`, `ticket/NNN-2-desc`.
- Delete your branch after the PR is merged or closed.

### Creating a branch

```bash
# From dev, create a ticket branch
git checkout dev
git pull origin dev
git checkout -b ticket/042-add-webhook-support
```

### Cleaning up after merge

```bash
# Delete local branch
git checkout dev
git branch -d ticket/042-add-webhook-support

# Delete remote branch
git push origin --delete ticket/042-add-webhook-support
```

Or with `gh`:
```bash
gh repo clone railstracks/animus
cd animus
git checkout dev
git checkout -b ticket/042-add-webhook-support
```

## Issues

### Filing an issue

Use the issue templates (`.github/ISSUE_TEMPLATE/`). Fill in all fields — incomplete issues will be labeled `needs-info` and won't be picked up.

Every issue should include:
- **Summary:** What needs to be done, in one or two sentences.
- **Motivation:** Why this matters.
- **Acceptance criteria:** Bullet list of what "done" looks like. Each item should be testable.
- **Related:** Links to related issues, PRs, or discussions.

### With `gh` CLI

```bash
# Create an issue
gh issue create --title "Add webhook support for GitHub events" \
  --body "$(cat .github/ISSUE_TEMPLATE/feature.md)"

# List open issues
gh issue list --state open

# View a specific issue
gh issue view 42

# Assign and label
gh issue edit 42 --add-label "enhancement,persistence"
```

### Labels

**Type labels:**
- `bug` — Something isn't working
- `enhancement` — New feature or request
- `documentation` — Docs improvements
- `refactor` — Code restructuring without behavior change

**Area labels:**
- `persistence` — Session/turn storage, Postgres, flush logic
- `adapter` — Social adapters (Bluesky, Telegram, Discord, etc.)
- `scheduler` — Task scheduling and cron
- `admin-ui` — Vue 3 admin interface
- `provider` — LLM provider integration
- `memory-system` — Memory consolidation, retrieval, ontology
- `security` — Auth, RBAC, signing, sandboxing
- `infrastructure` — Docker, deployment, CI

**Workflow labels:**
- `needs-info` — Missing required detail, not actionable yet
- `ready` — Triaged, acceptance criteria clear, ready to be picked up
- `in-progress` — Someone is working on this
- `blocked` — Waiting on another issue or decision

### Closing an issue

```bash
# Close with a comment
gh issue close 42 --comment "Fixed in #43"

# Close via PR (automatically closes when PR merges if "Closes #42" is in the PR body)
```

## Pull Requests

### Opening a PR

PRs target `dev`. The PR body should include:
- **What changed** — summary of the change
- **Why** — reference the issue (`Closes #NNN`)
- **Testing** — how you verified the change
- **Breaking changes** — anything that requires migration or config changes

```bash
# Push your branch and open a PR
git push -u origin ticket/042-add-webhook-support

gh pr create \
  --base dev \
  --head ticket/042-add-webhook-support \
  --title "feat: add webhook support for GitHub events" \
  --body "Closes #42

## What changed
- Added webhook handler in AdminServer
- Added configurable webhook secret validation

## Testing
- Built with cmake --build . -j\$(nproc) — all targets pass
- Tested with curl POST to /api/v1/webhooks/github

## Breaking changes
None. New endpoint, no existing behavior changed."
```

### Review checklist

Before requesting review:
- [ ] Builds clean (`cmake --build . -j$(nproc)`)
- [ ] Tests pass (run relevant test binaries)
- [ ] No new compiler warnings
- [ ] Commit messages are descriptive
- [ ] PR title follows convention: `feat:`, `fix:`, `docs:`, `refactor:`, `chore:`
- [ ] Breaking changes are documented
- [ ] New config fields have defaults

### After review

```bash
# Push changes requested in review
git add -A
git commit -m "fix: address review feedback — handle empty payload"
git push

# After merge, delete your branch
git checkout dev
git pull origin dev
git branch -d ticket/042-add-webhook-support
git push origin --delete ticket/042-add-webhook-support
```

## Commit Conventions

Use conventional commit prefixes:

| Prefix | Use for |
|--------|---------|
| `feat:` | New features |
| `fix:` | Bug fixes |
| `docs:` | Documentation only |
| `refactor:` | Code restructuring, no behavior change |
| `chore:` | Build, deps, tooling |
| `test:` | Test additions or fixes |

Keep commit messages in present tense, imperative mood: "add webhook support", not "added webhook support".

## For AI Agents

Agents working on Animus should use the `gh` CLI for all GitHub operations. The workflow is identical to human contributors:

1. **Read the issue:** `gh issue view NNN`
2. **Create a branch:** `git checkout dev && git checkout -b ticket/NNN-short-desc`
3. **Implement the change** on the branch
4. **Verify:** build clean, tests pass
5. **Commit and push:** descriptive commit messages, conventional prefixes
6. **Open PR:** `gh pr create --base dev ...`
7. **Address review feedback** if any
8. **Clean up branch** after merge

**Do not:**
- Commit directly to `main` or `dev`
- Force-push to shared branches
- Delete branches you didn't create
- Close issues you didn't fix
- Push secrets, credentials, or `.env` files

## Questions?

Open an issue with the `question` label, or join the [Discord](https://discord.gg/gjXsWYZgA) for discussion.
