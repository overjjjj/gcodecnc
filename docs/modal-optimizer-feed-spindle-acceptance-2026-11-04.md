# Modal feed and spindle restoration acceptance (2026-11-04)

Scope: the four-line change in `GCodeModalOptimizer.cpp`, regression additions
in `gcode_modal_optimizer_test.cpp`, and this record. Other pending machining,
selection, communication, simulation, and project changes are excluded.

## Defect and correction

Given `G1 X1 F200`, a protected cycle block containing `F1200`, then a return
to `G1 Z2 F200`, the optimizer previously removed the final `F200` because its
cached feed still contained the pre-cycle value. The same issue affected
spindle restoration after a protected spindle block.

Protected blocks now invalidate the feed and spindle caches. The next explicit
F/S setting is retained. Existing protected-block preservation and motion-mode
handling remain covered by the original tests.

The source matches the previously accepted 2026-10-23 version:

`B6820051DB15FB8F57936F23DA2295A93EA846A9966AB139D5AC7147E81C8D63`

Historical local evidence:
`D:/yanggao/gcodecnc/outputs/slot-overall-review-20261023/ACCEPTANCE.md`
and `changed-source-manifest.json`. The historical batch also contains other
fixes; its complete acceptance is not claimed for this isolated commit.

## Verification performed

An isolated source tree used baseline commit
`a1c22b24327f54e3f2d74cc9021a7c4c430fdae2`, the unchanged optimizer header,
and the selected optimizer implementation. No other pending implementation
changes or existing application objects were linked.

- Original test against baseline implementation: PASS, exit 0.
- Extended test against baseline implementation: expected FAIL, exit 1,
  reporting the missing post-cycle plunge-feed restoration.
- Identical extended test against corrected implementation: PASS, exit 0.
- Eight new checks cover G73, G76, G81, G83, G86, spindle restoration,
  comment words on a protected cycle block, and an M98 macro call.
- All eight original checks also pass in the extended candidate test.

Local build and execution logs are in
`D:/yanggao/gcodecnc/outputs/git-modal-sync-20261104`.
Executables and these local evidence directories are not part of the commit.

Compiler: `D:/qt5/Tools/mingw730_64/bin/g++.exe`, C++17,
Qt `D:/qt5/5.14.2/mingw73_64`, linked with Qt5Core only.
The compiler helper from the historical evidence directory supplied
`cc1plus.exe` and `collect2.exe` through the compiler's `-B` option.

The existing registered test remains the reproduction entry:

```powershell
cd untitled
./scripts/run_tests.ps1 -TestNames gcode_modal_optimizer_test
```

Use the matching Qt/MinGW toolchain on PATH. For this verification, the same
test sources were compiled directly into the isolated evidence directory
instead of writing to the application's debug directory.

This is a text-optimizer unit verification. It does not claim a fresh full
application build, real feature-board geometry acceptance, controller
compatibility, or machine cutting verification.
