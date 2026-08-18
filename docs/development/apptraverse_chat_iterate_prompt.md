Status: active
Plan: apptraverse_chat_plan.md
Progress: apptraverse_chat_progress.md
Stop signal: APPTRAVERSE_CHAT_BASELINE_COMPLETE

# Purpose

Repeatable prompt that always executes exactly one ready slice from the App Traverse Chat development track.

# Ready-to-paste user message

Continue the App Traverse Chat development track.

Read:
- docs/development/apptraverse_chat_plan.md
- docs/development/apptraverse_chat_progress.md
- docs/development/apptraverse_chat_iterate_prompt.md

Execute exactly the first ready slice from progress.

Before substantive work:
- verify prerequisites
- mark only that slice in_progress

Execute only that slice:
- no adjacent cleanup
- no next-slice implementation
- no speculative fixes
- no toolchain installation
- no branch changes outside the selected slice

Use only the canonical profile named by the slice.
Drive configure/build through `tools/runners/run_apptraverse_build.py`.
Do not construct long PowerShell or bash build command sequences.
Never run clean or rebuild unless the selected slice explicitly requires it.
Prefer incremental narrow-target builds.
Do not invent ad-hoc build directories or implicit compiler/generator fallbacks.
Update progress, evidence and the next ready slice in the same session.
Stop after one completed or typed-blocked slice.
Stop immediately if progress contains:
APPTRAVERSE_CHAT_BASELINE_COMPLETE

# Agent loop

1. Read plan, progress and iterate prompt.
2. Select exactly the first ready slice.
3. Verify prerequisites.
4. Mark it in_progress.
5. Execute only it.
6. Run only the proof named by that slice.
7. Record artifacts, build identity, blockers and evidence.
8. Mark done or blocked.
9. Select at most one next ready slice.
10. Stop.

# Build policy

- Execute one slice only.
- Python runner is the canonical orchestration entry point.
- Ninja is preferred for incremental C++ builds but is not mandatory.
- Windows Visual Studio 17 2022 is an explicit profile, not an implicit fallback.
- Windows compiler must be MSVC. No MinGW/GCC fallback.
- Never reuse a build directory configured for another compiler/generator; return `build_profile_conflict`.
- No ad-hoc build directories (`build2`, `build-new`, and similar).
- Incremental narrow target builds via the runner.
- Configure only when the canonical directory is missing, CMake inputs changed, or the slice owns configure.
- No clean/rebuild unless explicitly owned by the selected slice or requested by the user.
- No full CTest unless explicitly owned by the slice.
- No automatic compiler/generator fallback inside one invocation.
- No automatic SDK/tool installation.
- One retry maximum. A failure does not authorize clean, rebuild, a new directory, or another toolchain.
- External configure/build commands time out at 15 minutes (`command_timeout`).
- Full logs remain artifacts once ACT-S022 exists; Cursor should receive compact results.
- A documentation/configuration-only slice performs no configure, build, or tests.

# Git policy

- one branch per review slice when requested
- no force push
- no PR unless requested
- no commit/push unless selected slice requires it
- do not modify unrelated dirty checkout
- prefer a clean clone/worktree
- do not erase untracked user artifacts

# Completion packet template

Slice:
Acceptance IDs:
Files:
Build profile:
Build proof:
Runtime proof:
Typed blockers:
Known limits:
Next ready slice:
