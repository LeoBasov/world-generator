## Summary

<!-- What changes and why. Name the work package (A##) and plan section. -->

## Checklist

- [ ] Every new or changed source file has a unit test at the mirrored path under `tests/unit/`
- [ ] Acceptance tests of the work package pass in `ci.yml` on all primary runners
- [ ] `CHANGELOG.md` has an entry under `[Unreleased]`
- [ ] Golden hashes unchanged, or updated via a `golden/**` branch with an ADR explaining why the world changed
- [ ] clang-format and clang-tidy clean; no new warnings
- [ ] Determinism rules kept (counter RNG only, index tie-breaks, fixed reduction chunks)
- [ ] Public API changes are `noexcept`, return `Status`/`Expected` and are documented
