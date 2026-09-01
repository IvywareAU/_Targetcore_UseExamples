# Contributing to `_TargetCore_UseExamples`

## Before you spend time on a change

This repository is **not self-contained** — a fresh clone does not compile, because
`Msgcore/`, `TargetCore/`, `TargetFacade/` and `vsutils/` are peer directories in a
parent solution that is not published here. The README's ["The sibling
dependencies"](README.md#the-sibling-dependencies) lists every binding, including the
one (`$(KgnRoot)`) that only `RouteLoopbackTest` has. Until that is resolved an outside
contributor cannot build what they are changing, which makes anything beyond a
documentation fix hard to do well.

Issues and reports are welcome regardless.

## What this repository is for

These are **worked examples**, not a test suite and not a library. It holds five trees —
[`DirectExamples`](DirectExamples), [`FacadeExamples`](FacadeExamples),
[`ComExamples`](ComExamples), [`dotNetExamples`](dotNetExamples) and
[`PanamaJavaExamples`](PanamaJavaExamples) — which are the same harnesses reached through
five different bindings; the root [`README.md`](README.md) says which is which. That
changes what a good contribution looks like:

- A harness earns its place by answering **one question** that no other harness here
  answers, and by saying at the top of the file what that question is. Twelve harnesses
  that each demonstrate one mechanism are worth more than one that demonstrates twelve.
- The header comment is the deliverable as much as the code is. The existing files are
  the standard: what is being asked, what the framework source actually says about it
  (cited by file and line), what was verified rather than assumed, and what is still
  unknown.
- **Do not quietly fix the library from in here.** If a harness reveals a TargetCore
  defect, the harness records it and the fix goes in `TargetCore`. An example that works
  around a bug teaches the workaround, not the API.

## Sign your work — the Developer Certificate of Origin

Every commit must carry a `Signed-off-by` line:

```
Signed-off-by: Jane Developer <jane@example.com>
```

`git commit -s` adds it for you. Use your real name and an address you read.

That line means you certify the [Developer Certificate of Origin
1.1](https://developercertificate.org/): that you wrote the contribution or otherwise
have the right to submit it under the Apache License, Version 2.0, and that you
understand the contribution and its record are public and permanent.

**Why this is enforced from the first commit rather than added later.** Only a rights
holder can license code. Once a contribution arrives with no record of who held the
rights and under what terms, the project can no longer answer that question for its own
tree — and the option of ever relicensing, dual-licensing or granting an exception closes
permanently, because there is nobody identifiable to ask. A pull request without a
sign-off cannot be merged, no matter how good it is.

## Making a change

1. **One concern per commit.** A refactor and a behaviour change in the same commit
   cannot be reviewed, reverted or bisected independently.
2. **Write the message for someone reading it in five years.** The subject line as an
   imperative sentence, the body for the reasoning. The existing history is the standard
   to match: "Add ExplorerTest: the hub Explorer, asked what it is", "Keep one solution,
   license the tree, and finish RouteLoopbackTest". Both say what changed and why.
3. **Build both configurations**, `Debug` and `Release` × `x64`. Release is not a
   formality here: three of these harnesses have raced or timed out only in Release.
4. **Run `run_all.ps1` in both configurations** and put the resulting table in the pull
   request. A harness that has not been run is a hypothesis.
5. **Keep the exit-code contract.** `0` success, `1` setup, `2` assertion, `3` timeout —
   see the README. A new harness that returns `0` unconditionally cannot be adjudicated
   by anything, and `AlexTest` is the cautionary example, not the model.

## Adding a harness

A twelfth question belongs in **all five trees or in none**. The trees are worth reading
side by side only for as long as they ask the same questions; one tree drifting ahead is
what makes the comparison stop working.

Alongside the code, in each tree that has a solution:

- register it in that tree's `.sln` — `DirectExamples(2022).sln`,
  `FacadeExamples(2022).sln`, `ComExamples(2022).sln` — for **both** configurations. The
  `ci.yml` invariants job fails on a project no solution builds;
- install `_CrtSetReportHook(AssertReportHook)` unless the harness is interactive by
  design, or a debug `ASSERT` pops a modal dialog and hangs an unattended run instead of
  producing exit `2`;
- add it to the appropriate list in that tree's `run_all.ps1`, and to the `build.ps1` of
  `dotNetExamples` and `PanamaJavaExamples`, or state in its header why it cannot be run
  unattended;
- point `OutDir`/`IntDir` at the tree's shared `out\` root like every other project, and
  do not add a per-project `.sln`.

## Moving or renaming a tree

Every outward path in this repository is relative, so a tree that changes depth breaks
paths in four kinds of file at once: `.vcxproj`, `common\*.props`, `run_all.ps1` /
`build.ps1`, and the prose. Two of those fail loudly and two do not.

- The pins in `.github/ci/check_repo_invariants.py` are what catch it. They record the
  outward paths **and their depth** per tree; a level lost in a move shows up there
  rather than as `LNK1181` later.
- `git mv` the `.sln` too. A `.sln` never contains its own name, so no grep will tell you
  it was left behind — only a filename listing will, and `run_all.ps1` naming a file that
  no longer exists fails as "project file does not exist".

## Two things that will get a change rejected on sight

- **A new sibling dependency added silently.** The four bindings in the README are pinned
  by the invariants job precisely so that the standalone-build story cannot get worse
  without a reviewer noticing. If a change needs a fifth, change the README and the pin
  in the same commit and argue for it.
- **Deleting a comment that records a hazard** because the code around it was fixed.
  Rewrite it to say what is true now. Several of these headers exist only because
  somebody wrote down what bit them.

## License

By contributing, you agree that your contributions are licensed under the Apache License,
Version 2.0. See [`LICENSE`](LICENSE).
