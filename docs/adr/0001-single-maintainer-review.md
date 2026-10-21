# ADR 0001: No required approving review while there is a single maintainer

- Status: accepted
- Date: 2026-10-21
- Work package: A01

## Context

Plan section 6.8 asks for a branch ruleset on `main` that requires a merge via pull request with
one approving review from CODEOWNERS. The repository currently has one maintainer, who is the only
code owner. GitHub does not count an author's approval of their own pull request, so a required
review would block every merge.

## Decision

The ruleset in `.github/rulesets/main.json` requires a pull request with 0 approving reviews. All
other rules of section 6.8 stay: the every-push `ci.yml` jobs are required status checks on the
branch's head commit, force pushes and deletion of `main` are blocked, and there are no bypass
actors.

## Consequences

A pull request with a failing required check still cannot be merged, which is the A01 acceptance
criterion. Review becomes a self-review against `.github/pull_request_template.md`.

When a second developer joins, set `required_approving_review_count` to 1 and
`require_code_owner_review` to true, add them to CODEOWNERS, and mark this ADR superseded.

## Alternatives considered

- One required approval as planned: it blocks all merges with a single maintainer.
- One required approval with an admin bypass: the bypass also skips the required status checks,
  which defeats the main purpose of the ruleset.
