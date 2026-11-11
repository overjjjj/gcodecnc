# G-code feed and comment safety acceptance (2026-11-11)

Scope: the existing isolated change in GCodeSafetyValidator.cpp, nine new
checks in the existing gcode_safety_validator_test.cpp, and this record.
Other pending machining, selection, simulation, communication, and project
changes remain outside this commit.

## Defects and correction

The baseline accepted explicit zero or negative numeric F words. The selected
source change adds a blocking diagnostic for parsed non-positive feed values.
It does not claim a general rejection of every malformed or symbolic F token.

The baseline truncated a line at the first semicolon before removing parenthesis
comments. A semicolon inside a parenthesis comment could therefore hide executable
words after the closing parenthesis, including unsupported G91 or G20 modes.
Parenthesis comments are now replaced with spaces before the semicolon tail is
removed, preserving the following executable text for the existing checks.

## Source identity

The selected implementation matches the 2026-10-23 accepted source SHA256:
E5C90DA79E0731A9442A0DC4169F507221EA29DDF8802823467C3868AF28BFC4

Historical local evidence:
D:/yanggao/gcodecnc/outputs/slot-overall-review-20261023/ACCEPTANCE.md
and changed-source-manifest.json. This commit does not claim acceptance of
all other fixes contained in that historical batch.

## Verification performed

An isolated source tree used baseline commit
52ec95760c264920cc4a49704591236ee3ba984d and its unchanged validator header.
Only the validator and test were compiled, with Qt5Core. No pending application
objects or untracked product dependencies were linked.

- Original registered test with baseline implementation: exit 0.
- Extended test with baseline implementation: expected exit 1, reporting six
  regression failures (three non-positive feeds, compact F0, and two hidden modes).
- Identical extended test with the corrected implementation: exit 0, including
  all existing checks and nine new checks.
- Positive cases retain a small positive fractional feed, ignore unsafe-looking
  words inside parentheses, and ignore words after a real semicolon comment.

Local evidence is in D:/yanggao/gcodecnc/outputs/git-safety-sync-20261111.
The compiler was D:/qt5/Tools/mingw730_64/bin/g++.exe, C++17, using
Qt D:/qt5/5.14.2/mingw73_64 and the historical compiler-helper through -B.
Executables, compiler helpers, and evidence directories are excluded from Git.

The existing registered entry remains:

```powershell
cd untitled
./scripts/run_tests.ps1 -TestNames gcode_safety_validator_test
```

Use the matching Qt/MinGW toolchain on PATH. This verification compiled the
same sources directly into the isolated evidence directory to avoid writing
to the application's debug directory.

This is a validator unit verification. It does not claim a fresh full-application
build, real feature-board geometry acceptance, or machine cutting verification.
