# Copyright © 2026 Khrustal & Mann
#              MELBOURNE, VICTORIA, AUSTRALIA, 3000
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied. See the License for the specific language governing
# permissions and limitations under the License.
#
# ---------------------------------------------------------------------------
# check_repo_invariants.py -- the checks this repository can make about ITSELF,
# with no sibling checkout, no compiler, no MSBuild and no JDK.
#
# WHY IT EXISTS. This repository holds five BINDING trees -- DirectExamples,
# FacadeExamples, ComExamples, dotNetExamples and PanamaJavaExamples -- which
# are the same eleven harnesses reached through five different bindings
# (README.md), plus two that vary the SUBJECT rather than the binding and so are
# not among the five: ErrorReportingExamples (where a diagnostic goes) and
# SecurityExamples (the posture a hub runs in).
# None of them builds standalone: Msgcore/, TargetCore/,
# TargetFacade/ and vsutils/ are peer directories of a parent MSCS solution that
# is not published here. So a per-push workflow has a choice between verifying
# nothing and verifying bookkeeping, and bookkeeping is worth more than zero.
#
# WHAT THIS PROVES
#   1. The legal/release files the repository is expected to carry are present,
#      including ArchitectureFAQ.md -- which lives at the ROOT rather than in
#      any one tree, because it answers questions about the mesh that hold for
#      all five, and the sibling _Msgcore_UseExamples repository links to it.
#      Every tree carries the README.md and the runner the root README links to.
#   2. EACH SOLUTION AND ITS PROJECT FILES AGREE. Every .vcxproj on disk is in
#      its tree's .sln for BOTH configurations, and every project a .sln names
#      exists. A harness added to a tree but not to its solution is invisible:
#      nothing builds it, so it rots silently. The .sln NAME matters as much as
#      its contents -- run_all.ps1 names the file, and a rename that misses one
#      leaves msbuild saying "project file does not exist". A .sln never
#      contains its own name, so no grep will tell you; only this will.
#   3. Every source a project compiles exists where the project says it does --
#      including the sources reached from OUTSIDE this repository, whose set is
#      PINNED per tree.
#   4. The paths the .props files build out of $(MSBuildThisFileDirectory) are
#      pinned too, because ComExamples and FacadeExamples reach outward through
#      those properties rather than through literal paths in each .vcxproj.
#
#      Checks 3 and 4 are the standalone-build contract, and they also guard the
#      shape of this repository. Every one of these paths is relative and every
#      tree is one directory deeper than it was when it was a repository of its
#      own, so a `..\..\` that should have become `..\..\..\` fails HERE, in
#      seconds, on a runner with no compiler -- rather than as LNK1181 on
#      somebody's machine.
#   5. Every relative link in the shipped Markdown resolves to a file that
#      exists. A README pointing at a document that was renamed or deleted is
#      cheap to introduce and expensive to notice by hand.
#
# WHAT THIS DOES NOT PROVE
#   Nothing here compiles, links or runs anything. Not one line of C++, C#, Java
#   or PowerShell in this repository is even parsed. A harness can be in its
#   solution, name sources that all exist, and still not compile -- and if it
#   does compile it can still fail at run time. That verification is
#   solution-build.yml's job, it needs the siblings, and it is
#   workflow_dispatch-only for exactly that reason.
# ---------------------------------------------------------------------------

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# The solution configurations every project must be registered for. Missing one
# is the failure mode this catches: a project that builds in Debug and silently
# is not built in Release looks fine until a release run.
CONFIGURATIONS = ("Debug|x64", "Release|x64")

# Files the COMBINED repository carries once, at its root, for all five trees.
REQUIRED_ROOT_FILES = [
    "LICENSE",            # Apache-2.0, referenced from README.md "License"
    "README.md",
    "CONTRIBUTING.md",
    "ArchitectureFAQ.md", # cited by every tree, and by the sibling _Msgcore repo
]


class Tree:
    """One binding tree, and what is true about that one only."""

    def __init__(self, name, solution, required, ext_sources, ext_dirs,
                 props_paths=frozenset()):
        self.name = name
        self.dir = ROOT / name
        # dotNetExamples and PanamaJavaExamples are built by csc and javac out
        # of build.ps1, not by MSBuild, so they have no solution and checks
        # [2], [3] and [4] do not apply to them.
        self.solution = (self.dir / solution) if solution else None
        self.required = required
        self.ext_sources = set(ext_sources)
        self.ext_dirs = set(ext_dirs)
        self.props_paths = set(props_paths)


# THREE leading `..\` and not two, throughout: a project file sits at
# <repo>/<Tree>/<Harness>/, so the MSCS solution root is three levels up. It was
# two while each tree was a repository of its own.
TREES = [
    Tree(
        "DirectExamples", "DirectExamples(2026).sln",
        ["README.md", "run_all.ps1", "About.md"],
        {r"..\..\..\vsutils\DelayLoadReport.cpp"},
        {
            r"..\..\..\Msgcore",
            r"..\..\..\TargetCore",
            r"..\..\..\lib\$(Platform)\$(Configuration)",
            r"..\..\..\lib\$(Platform)",
            r"..\..\..\lib",
            # RouteLoopbackTest's alone: it links treehub_runtime from a KGN
            # project that is not part of the MSCS tree at all, which is why
            # that one harness does not build on most machines and why
            # run_all.ps1 treats a missing RouteLoopbackTest.exe as SKIP rather
            # than as a failure.
            r"$(KgnRoot)\treehub_runtime\include",
            r"$(KgnRoot)\build\x64-debug\Debug",
            r"$(KgnRoot)\build\x64-debug\Release",
        },
    ),
    # ErrorReportingExamples is NOT one of the five bindings -- it varies the
    # subject (where a diagnostic goes) rather than the way the library is
    # reached. It is pinned here all the same, because the thing check [3]
    # protects against is a project file quietly acquiring a new outward path,
    # and that risk does not care why a tree exists.
    Tree(
        "ErrorReportingExamples", "ErrorReportingExamples(2026).sln",
        ["README.md", "run_all.ps1"],
        {r"..\..\..\vsutils\DelayLoadReport.cpp"},
        {
            r"..\..\..\Msgcore",
            r"..\..\..\TargetCore",
            r"..\..\..\lib\$(Platform)\$(Configuration)",
            r"..\..\..\lib\$(Platform)",
            r"..\..\..\lib",
        },
    ),
    # SecurityExamples is the other non-binding tree: its two harnesses hold
    # the binding fixed -- plain C++ against TargetCore.lib, exactly as
    # DirectExamples is -- and vary the SECURITY POSTURE instead, MixConTest
    # opting out of auth and sealing where MixConTestAuth provisions for them.
    # Its outward set is DirectExamples' minus $(KgnRoot), and it is pinned for
    # the same reason ErrorReportingExamples is: both harnesses arrived here as
    # loose directories under MSCS\, one level up from where they now sit, so
    # every `..\..\` that had to become `..\..\..\` is checked here rather
    # than left to fail as LNK1181 on somebody's machine.
    Tree(
        "SecurityExamples", "SecurityExamples(2026).sln",
        ["README.md", "run_all.ps1"],
        {r"..\..\..\vsutils\DelayLoadReport.cpp"},
        {
            r"..\..\..\Msgcore",
            r"..\..\..\TargetCore",
            r"..\..\..\lib\$(Platform)\$(Configuration)",
            r"..\..\..\lib\$(Platform)",
            r"..\..\..\lib",
        },
    ),
    # ComExamples and FacadeExamples name nothing external in their .vcxproj
    # files at all: every outward path is a property defined once in
    # common\*.props, which is what check [4] pins.
    Tree(
        "FacadeExamples", "FacadeExamples(2026).sln",
        ["README.md", "run_all.ps1"],
        set(), set(),
        # A raw string cannot end in a backslash, hence the doubled ones.
        props_paths={
            "$(MSBuildThisFileDirectory)..\\",                      # tree root
            "$(MSBuildThisFileDirectory)..\\..\\..\\TargetFacade\\",
            "$(MSBuildThisFileDirectory)..\\..\\..\\bin\\",
        },
    ),
    Tree(
        "ComExamples", "ComExamples(2026).sln",
        ["README.md", "run_all.ps1"],
        set(), set(),
        props_paths={
            "$(MSBuildThisFileDirectory)..\\",
            "$(MSBuildThisFileDirectory)..\\..\\..\\TargetFacade\\",
            "$(MSBuildThisFileDirectory)..\\..\\..\\bin\\",
        },
    ),
    Tree("dotNetExamples", None, ["README.md", "run_all.ps1", "build.ps1"],
         set(), set()),
    Tree("PanamaJavaExamples", None, ["README.md", "run_all.ps1", "build.ps1"],
         set(), set()),
]

failures: list[str] = []


def fail(check: str, message: str) -> None:
    failures.append(f"{check}: {message}")
    print(f"  FAIL  {message}")


def ok(message: str) -> None:
    print(f"  ok    {message}")


def read(path: Path) -> str:
    # Visual Studio writes .sln, .vcxproj and .props as UTF-8 with a BOM.
    return path.read_text(encoding="utf-8-sig")


# ---------------------------------------------------------------------------
# 1. Release-engineering / legal files, and per-tree documentation
# ---------------------------------------------------------------------------
def check_required_files() -> None:
    print("[1] required files")
    for name in REQUIRED_ROOT_FILES:
        if (ROOT / name).is_file():
            ok(f"{name} present at the repository root")
        else:
            fail("required-files", f"{name} is missing from the repository root")

    for tree in TREES:
        if not tree.dir.is_dir():
            fail("required-files", f"{tree.name}/ is missing entirely")
            continue
        for name in tree.required:
            if not (tree.dir / name).is_file():
                fail("required-files", f"{tree.name}/{name} is missing")
        if tree.solution is not None and not tree.solution.is_file():
            fail("required-files",
                 f"{tree.name}/{tree.solution.name} is missing -- run_all.ps1 "
                 f"names it, so msbuild would fail with 'project file does not "
                 f"exist'. A .sln never contains its own name, so only a "
                 f"filename check catches a rename that missed it.")
        else:
            ok(f"{tree.name}: required files present")


# ---------------------------------------------------------------------------
# 2. Each solution and its project files agree
# ---------------------------------------------------------------------------
SLN_PROJECT_RE = re.compile(
    r'^Project\("\{[0-9A-Fa-f-]+\}"\)\s*=\s*"([^"]+)",\s*"([^"]+)",\s*"\{([0-9A-Fa-f-]+)\}"',
    re.M,
)
SLN_CONFIG_RE = re.compile(r"\{([0-9A-Fa-f-]+)\}\.([^.]+)\.Build\.0\s*=")


def check_solution_parity(tree: Tree) -> dict[str, Path]:
    if tree.solution is None:
        ok(f"{tree.name}: no solution by design (driven from build.ps1)")
        return {}
    if not tree.solution.is_file():
        return {}                                   # already reported in [1]

    sln = read(tree.solution)
    entries = SLN_PROJECT_RE.findall(sln)
    if not entries:
        fail("solution", f"{tree.name}: no Project(...) entries parsed -- the "
                         f"parser is broken, not the solution")
        return {}

    projects: dict[str, Path] = {}
    guids: dict[str, str] = {}
    for name, relpath, guid in entries:
        path = tree.dir / relpath.replace("\\", "/")
        projects[name] = path
        guids[name] = guid.upper()
        if not path.is_file():
            fail("solution", f"{tree.name}: the solution lists '{name}' at "
                             f"'{relpath}', which does not exist")

    # Every .vcxproj on disk must be IN the solution. This is the direction that
    # actually goes wrong: adding a project file and forgetting the solution.
    on_disk = {p.resolve() for p in tree.dir.glob("*/*.vcxproj")}
    in_sln = {p.resolve() for p in projects.values()}
    for orphan in sorted(on_disk - in_sln):
        fail("solution", f"'{orphan.relative_to(ROOT)}' exists but no solution "
                         f"builds it -- nothing compiles it, so it rots unnoticed")
    if not (on_disk - in_sln):
        ok(f"{tree.name}: all {len(on_disk)} project files on disk are in "
           f"{tree.solution.name}")

    # ... and for both configurations. Count this check's own failures rather
    # than testing the global list, which by here may already hold failures from
    # check [1] and would suppress this line for an unrelated reason.
    built: dict[str, set] = {}
    for guid, cfg in SLN_CONFIG_RE.findall(sln):
        built.setdefault(guid.upper(), set()).add(cfg)
    unbuilt = 0
    for name, guid in sorted(guids.items()):
        have = built.get(guid, set())
        for cfg in CONFIGURATIONS:
            if cfg not in have:
                unbuilt += 1
                fail("solution", f"{tree.name}: '{name}' is not set to BUILD in "
                                 f"{cfg} -- it will be silently skipped there")
    if unbuilt == 0:
        ok(f"{tree.name}: all {len(guids)} projects build in "
           f"{', '.join(CONFIGURATIONS)}")

    return projects


# ---------------------------------------------------------------------------
# 3. Every source a project names exists; the external set is pinned
# ---------------------------------------------------------------------------
CL_COMPILE_RE = re.compile(r'<ClCompile\s+Include="([^"]+)"')
CL_INCLUDE_RE = re.compile(r'<ClInclude\s+Include="([^"]+)"')
INC_DIRS_RE = re.compile(r"<AdditionalIncludeDirectories>([^<]*)</AdditionalIncludeDirectories>")
LIB_DIRS_RE = re.compile(r"<AdditionalLibraryDirectories>([^<]*)</AdditionalLibraryDirectories>")


def is_external(spec: str) -> bool:
    """A path that leaves the repository: either it escapes upward past the tree
    it is in, or it starts at an MSBuild property we cannot resolve."""
    return spec.startswith("..\\..\\") or spec.startswith("$(")


def compare(tree: Tree, label: str, found: set, expected: set) -> None:
    if found == expected:
        ok(f"{tree.name}: {label} unchanged ({len(found)} entries)")
        return
    added = sorted(found - expected)
    gone = sorted(expected - found)
    fail("pin", f"{tree.name}: {label} changed (added {added}, no longer present "
                f"{gone}). If a path merely got shallower or deeper, the tree "
                f"moved and something did not follow. Otherwise update "
                f"README.md 'The sibling dependencies' and this pin together.")


def check_source_paths(tree: Tree, projects: dict[str, Path]) -> None:
    if not projects:
        return

    external_sources: set[str] = set()
    external_dirs: set[str] = set()
    checked = 0

    for name, proj in sorted(projects.items()):
        if not proj.is_file():
            continue                                   # already reported in [2]
        text = read(proj)

        for spec in CL_COMPILE_RE.findall(text) + CL_INCLUDE_RE.findall(text):
            if is_external(spec):
                external_sources.add(spec)
                continue                               # not in this repository
            checked += 1
            if not (proj.parent / spec.replace("\\", "/")).is_file():
                fail("sources", f"{tree.name}: {name} compiles '{spec}', which "
                                f"does not exist")

        for block in INC_DIRS_RE.findall(text) + LIB_DIRS_RE.findall(text):
            for spec in block.split(";"):
                spec = spec.strip()
                if not spec or spec.startswith("%("):
                    continue
                if is_external(spec):
                    external_dirs.add(spec)

    ok(f"{tree.name}: {checked} in-repository source references resolve")
    compare(tree, "external sources compiled", external_sources, tree.ext_sources)
    compare(tree, "external include/library directories", external_dirs, tree.ext_dirs)


# ---------------------------------------------------------------------------
# 4. The relative paths the .props files construct are pinned
# ---------------------------------------------------------------------------
PROP_VALUE_RE = re.compile(r"<[A-Za-z_][\w.-]*(?:\s[^>]*)?>([^<]*\.\.[\\/][^<]*)</")


def check_props_paths(tree: Tree) -> None:
    found = set()
    for props in sorted(tree.dir.rglob("*.props")):
        for value in PROP_VALUE_RE.findall(read(props)):
            value = value.strip()
            if value:
                found.add(value)
    if not found and not tree.props_paths:
        ok(f"{tree.name}: no .props file builds a relative path")
        return
    compare(tree, "relative paths built in .props", found, tree.props_paths)


# ---------------------------------------------------------------------------
# 5. Relative Markdown links resolve
# ---------------------------------------------------------------------------
LINK_RE = re.compile(r"\[[^\]]*\]\(\s*(<[^>]*>|[^()\s]*(?:\([^()]*\)[^()\s]*)*)\s*\)")


def check_markdown_links() -> None:
    print("[5] markdown links resolve to files that exist")
    checked = 0
    docs = sorted(set(ROOT.glob("*.md")) | set(ROOT.glob("*/*.md"))
                  | set(ROOT.glob("*/*/*.md")))
    for md in docs:
        if ".github" in md.parts or "tools" in md.parts:
            continue
        for raw in LINK_RE.findall(read(md)):
            target = raw.strip()
            if target.startswith("<") and target.endswith(">"):
                target = target[1:-1]
            target = target.split("#", 1)[0].strip()
            if not target:
                continue                               # pure anchor
            if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
                continue                               # http:, mailto:, ...
            if target.startswith("/"):
                continue
            resolved = (md.parent / target).resolve()
            # A link that leaves the repository entirely -- the sibling
            # _Msgcore_UseExamples repository, or TargetFacade\, which resolve
            # inside the parent MSCS tree and cannot resolve in a standalone
            # clone. Failing on those would be failing on the documented layout.
            # Links BETWEEN the five trees stay inside and ARE checked.
            if not str(resolved).startswith(str(ROOT)):
                continue
            checked += 1
            if not resolved.exists():
                fail("md-links", f"{md.relative_to(ROOT)} links to '{target}', "
                                 f"which does not exist")
    ok(f"{checked} relative links checked")


def main() -> int:
    print(f"repo: {ROOT}")
    check_required_files()
    print("[2] solution / project parity")
    parity = {t.name: check_solution_parity(t) for t in TREES}
    print("[3] project sources exist; external references are pinned")
    for tree in TREES:
        check_source_paths(tree, parity[tree.name])
    print("[4] relative paths constructed in .props are pinned")
    for tree in TREES:
        check_props_paths(tree)
    check_markdown_links()

    if failures:
        print(f"\n{len(failures)} invariant(s) violated:")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("\nAll repository invariants hold. "
          "NOTE: nothing was compiled -- see the header of this file.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
