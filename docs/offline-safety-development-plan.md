# CNEXT-CAM Offline Safety Development Plan

## Objective

Deliver a reproducible offline CAM safety loop before adding more machining strategies or machine-control features. The loop is complete when a project can be restored from its unchanged STEP source, selected hole and slot operations generate traceable controller-targeted output, unsafe output cannot be exported, and the final acceptance model can be verified through the agreed three-level process.

## Confirmed product boundaries

- CQ8 is the production acceptance controller. The standalone CNEXT-CAM application in `untitled/` will own the future PC-side CQ8 communication and online-control module; no other software project is part of the product. Siemens 840D and Fanuc remain optional compatibility and regression targets.
- CQ8 deployment is deferred while the CAM foundation is completed. The CAM codebase reserves a versioned CQ8 Integration Port but does not yet connect, download, start motion, or control machine state.
- The first CAM acceptance scope is fixed to one Setup, three-axis STEP input, stable hole and slot loops, and only the plane or contour operations required by the WH250852 Acceptance Model.
- Generated motion uses absolute coordinates (`G90`) only. `G91` is disabled until every parser, validator, simulator, and postprocessor supports incremental modal state.
- One confirmed Setup and one WCS (`G54` by default) are supported for first acceptance. Side, back, or second-clamping work must be rejected with a clear message.
- Users select the Setup and machining features explicitly. STEP colors and the acceptance-model filename do not define machining scope.
- Feature recognition and process recommendations may be automatic, but strategy, tool, and process-plan choices remain human decisions. Only explicitly confirmed Operation Proposals become Machining Operations eligible for G-code generation.
- Operation Proposals exclude geometrically or setup-infeasible choices, may highlight and prefill one recommendation, and require an explicit “confirm and add operation” action. Advanced edits remain subject to non-bypassable geometry and safety validation.
- The first acceptance includes user-selected Process Templates organized by material, strategy, and compatible tool range. Applying a template copies editable suggested values into an Operation Proposal; confirmed operations and Program Snapshots retain their final values independently of later template changes.
- Safety validation failures block export and machine sending. Unsafe programs may be simulated with a persistent unverified warning and no ordinary bypass.
- Roughing, holes, and bottom finishing use CAM-computed tool-center paths under `G40`. Contour and slot-wall finishing may use `G41/G42` only with validated activation and cancellation moves.
- First-stage simulation is path safety validation, not full material-removal verification. Dynamic stock, holder/fixture swept-volume collision, and multi-Setup simulation are deferred.
- The Execution Preview is derived only from final controller-targeted output. It expands supported cycles, macros, and subprograms, links source lines to operations and tool position, and blocks feature-bound, depth, low-rapid, tool-envelope, or stock-bottom violations.
- Safe start codes are configured through a controlled Machine Profile template, not arbitrary per-operation text.
- Siemens output is one main MPF plus SPF subprograms. A subprogram is created only for logic repeated at least twice.
- Subprograms may receive geometry, position, depth, and cutting parameters. Tool change, spindle, coolant, WCS, and safety state remain in the MPF.

## Test model inventory

| Level | Model | SHA-256 | Purpose |
|---|---|---|---|
| Foundation | `untitled/测试模型/孔.stp` | `8285E06022B3A6FA447AB6060811F647E3ED35FE4D858A125C7CF1E1E33896A6` | Hole recognition, cycles, program packaging, simulation |
| Foundation | `untitled/测试模型/槽.stp` | `39D1BB74350AA020AD1AD688BC3C32F12CE13C57DB0C1278E9BE79BBC7A38045` | Open/blind slot geometry, entry/retract, boundaries, wall finishing |
| Final | `untitled/测试模型/验收/WH250852-模板紅色面加工（零件3）.STEP` | `7728E246C323C2B4BD9D6BDE7066BC90DF2E94CED7AD2001D896012C86A1FA50` | Production-representative end-to-end acceptance |

## Delivery sequence

### Standard local verification command

Run the complete qmake, Debug build, and test pipeline from `untitled/`:

```powershell
.\scripts\build_and_test.ps1
```

Use `-QtDir` and `-MinGwDir` only when the approved Qt 5.14.2 toolchain is installed elsewhere. The script validates explicit tool paths before regenerating build files, preventing a different qmake from being selected through `PATH`.

### Phase 0 — Reproducible baseline

Goal: make the current source tree buildable and reviewable without relying on stale generated files.

Status on 2026-07-28: the explicit-toolchain qmake/build/test pipeline is implemented and passes repeatedly. Decomposing the pre-existing large working-tree change into reviewable commits remains pending and requires a separate commit-scope decision.

Work:

- Add one documented command that regenerates qmake files, builds Debug, and runs all tests.
- Ensure new sources in `untitled.pro` are present in a clean build.
- Separate the current large working-tree change into reviewable commits before feature work resumes.
- Add a build check that starts from regenerated build files rather than the existing `Makefile.Debug`.

Verify:

- A clean qmake/build completes without undefined references.
- All existing tests pass from the same command.
- A second run is idempotent and does not require manual Qt Creator actions.

### Phase 1 — Reliable project restoration

Goal: make `.cnext` projects safe to reopen and prevent stale geometry reuse.

Status on 2026-07-29: the Phase 1 persistence baseline is implemented. Project format 2.0 stores the STEP SHA-256 fingerprint, feature angle and face associations, Setup rotation, active region, WCS, the active Machine Profile, and complete Program Package snapshots. A package snapshot records its main MPF filename plus every MPF/SPF file's role, filename, content, and SHA-256 while retaining the legacy single-file G-code text. Missing or changed sources can be relinked: identical fingerprints preserve operations, while changed or unverifiable sources are re-recognized and invalidate previous operations and Program Snapshots. Legacy projects without package data continue to load with empty package fields and safe Setup/Siemens defaults. Front-Setup confirmation now accepts separate coplanar STEP face patches, and repeated front-setting composes the new alignment with the persisted Setup rotation instead of replacing it. Explicit migration tooling remains pending.

Work:

- Persist the source STEP path, SHA-256 fingerprint, schema version, active Setup, operations, Program Snapshots, and Machine Profile reference.
- Re-import the STEP on load before exposing features or operations to the UI.
- Block generation when the source is missing.
- When the source fingerprint changes, re-recognize features and mark old operations and Program Snapshots invalid.
- Preserve required feature identity and orientation data, including angle and face association or a stable replacement identifier.
- Return and display real save/load errors; never emit success after failure.
- Add schema migration tests for existing project format versions.

Verify:

- Save, close, reopen, and restore `孔.stp` and `槽.stp` in a fresh process.
- Loading after another model never reuses the previous mesh.
- Missing and changed source files produce deterministic blocking states.
- Corrupt or unwritable project files report failure and do not mutate the active project.

### Phase 2 — Output safety contract

Goal: validate the final modal and motion state that will be exported.

Status on 2026-07-29: the first safety slices are implemented. Machine Profile safe-start blocks are editable through a validated UI and are used by Siemens/Fanuc postprocessors; output always resolves the active WCS and enforces `G90`. The validator rejects any `G91`, tracks modal `G0/G1/G2/G3`, rejects XY rapid motion until a known positive Z has been established, and evaluates explicit and inherited rapid moves at the destination Z. It tracks live spindle, coolant, tool-change, and cutter-compensation state: later tool changes require positive Z plus prior `M5/M9`, final `M30` requires spindle and coolant off, and `G41/G42/G40` require linear XY lead transitions. Siemens and Fanuc now emit `M5/M9` before later tool changes. Circle, general contour-finish, open-contour, closed-contour, open-slot, and blind-slot generators bind machine compensation to their actual lead moves and pass the same safety gate in strategy-level tests. The deterministic Siemens Program Package builder emits one named MPF, extracts only sections repeated at least twice into parameterized `PROC`/`RET` SPF files, fingerprints every file, rejects machine state inside SPF bodies, persists its files in Program Snapshots, and exports a stored package to a selected directory after validating filenames and hashes. Automatic operation-to-section extraction, automatic package population during generation, geometry-aware cross-feature tracking, and expanded package validation remain pending.

Work:

- Replace line-presence checks with a small modal-state parser for `G0/G1/G2/G3`, `G17`, `G20/G21`, `G40/G41/G42`, `G49`, `G54-G59`, `G80`, `G90`, `G94`, tool, spindle, and coolant state.
- Evaluate combined-axis rapid lines using the destination Z on that line.
- Disable `G91` in the UI and postprocessor options.
- Define controlled Machine Profile fields for the safe start template and reject conflicting or missing states.
- Validate tool change before spindle start, safe retract before cross-feature rapid moves, cutter compensation lead-in/out, and shutdown order.
- Validate the expanded Siemens program package, not only individual MPF/SPF source strings.

Verify:

- Tests cover `G0 X... Y... Zsafe` and `G0 X... Y... Zcut` on one line.
- Tests cover modal carry-over, comments, compact word formatting, multiple tools, fixed cycles, and `G41/G42/G40` transitions.
- Unsafe output cannot be exported or sent; simulation visibly remains unverified.

### Phase 3 — Extract application services

Goal: remove CAM workflow orchestration from `MainWindow` without a wholesale rewrite.

Status on 2026-08-03: the first two `ProgramGenerationService` slices are implemented. Strategy execution, consecutive compatible-hole batching, tool resolution, controller postprocessing, operation trace comments, source-operation collection, final G-code safety validation, and complete Program Snapshot construction now run behind a UI-independent service. A successful Siemens result includes a deterministic main MPF whose content is exactly the validated final G-code plus its SHA-256; failures return no partial snapshot or package. `MainWindow` still owns Setup-specific prechecks, persistence of the returned snapshot, and result presentation. Automatic operation-to-section extraction and parameterized SPF creation remain pending and will be added through a separate golden-output-tested slice.

Work:

1. Extract `ProjectPersistenceService` for project restoration and schema handling.
2. Extract `GCodeValidationService` for Machine Profile and safety policy.
3. Extract `ProgramGenerationService` for operation execution, postprocessing, SPF extraction, and Program Snapshot creation.
4. Keep widgets responsible for input, presentation, and user confirmation only.

Verify:

- Each extraction begins with characterization tests of existing behavior.
- `MainWindow` no longer directly coordinates strategy execution, postprocessing, persistence, and safety validation.
- No strategy or UI behavior is changed merely to complete the extraction.

### Phase 4 — Hole and slot stable loop

Goal: establish the foundation regression gate using the committed small models.

Status on 2026-08-03: the committed hole STEP fixture now has a blocking OCC regression keyed by its SHA-256. It fixes the top face at `Z0`, the stock bottom at `Z-10`, and the exact centers, radii, depth, Z-axis alignment, face associations, and active-Setup region of three through holes. The test exposed and now prevents an exterior R5 corner cylinder from being classified as a fourth tapped hole by requiring a hole candidate to cover a nearly closed `2π` cylindrical face using restricted face bounds. The OCC region classifier now treats dual-opening through holes separately from one-sided holes: lateral axes remain `Side`, while Z-aligned holes use their center distance to the current Setup projection limits and choose `Front` on a tie. A real-strategy batch slice also verifies that two compatible peck-drilling operations generate one Siemens `MCALL CYCLE83` definition with two position calls, preserve every confirmed operation ID, pass final-output safety validation, and remain byte-identical to the stored main MPF. Human-confirmed fixture operations, other cycle families, unsafe mutations, and final-G-code simulation assertions remain pending.

Hole checks:

- Recognized side and depth match the confirmed Setup.
- Blind and through depths are distinct and controlled.
- Siemens `CYCLE81/82/83/84/85` arguments and expansion are verified.
- Repeated compatible holes use an SPF only when it reduces repeated logic.

Slot checks:

- Center, length, width, depth, angle, open side, and active face are recorded in golden expectations.
- Roughing cutter-center limits account for tool radius and stock.
- Ramp feasibility limits effective step-down or rejects the operation.
- Every low-Z rapid, entry, retract, bottom finish, and wall finish is checked.
- `G41/G42` wall finishing has sufficient lead-in/out and returns to `G40` before unrelated motion.

Verify:

- Both models pass recognition fixtures, Siemens golden Program Package comparison, safety validation, and final-G-code simulation.
- Deliberately unsafe mutations fail the expected check.

### Phase 5 — Final acceptance model

Goal: generate the selected red-face machining scope of the WH250852 model under one confirmed Setup.

Work:

- Import the committed Acceptance Model and verify its fingerprint.
- Let the user explicitly select the Setup and machining features.
- Reject selected features outside the active front Setup.
- Generate a stable main MPF and reusable SPF files with deterministic ASCII names.
- Record source operations, Machine Profile version, tool data, parameters, and file hashes in the Program Snapshot.

Verify:

1. CNEXT-CAM automatic validation and final-G-code path simulation pass.
2. The exported MPF/SPF package passes Siemens 840D syntax and trajectory simulation.
3. First machine use is dry run, single block, and reduced rapid/feed override before any production cutting.

## Acceptance matrix

| Capability | Automated evidence | Manual evidence | Blocking |
|---|---|---|---|
| Clean build | qmake/build/test command | None | Yes |
| Project restore | save/load and changed-source tests | Reopen both foundation models | Yes |
| Safe modal template | Machine Profile tests | Review generated header | Yes |
| Rapid and depth safety | modal parser tests | Inspect reported line and position | Yes |
| Cutter compensation | golden G-code and state tests | Siemens backplot review | Yes when used |
| MPF/SPF packaging | deterministic package tests | Open complete package in simulator | Yes |
| Hole model | recognition and golden-output tests | Path review | Yes |
| Slot model | geometry-bound and golden-output tests | Entry/retract and wall review | Yes |
| Acceptance Model | fingerprint and package checks | Siemens simulation and dry run | Final gate |

## Explicitly deferred

- DXF programming and general-purpose CAD drawing or repair tools.
- Dynamic milling, engraving, broad 3D surface/fillet machining, and other strategy breadth not required by the Acceptance Model.
- New machining strategy families beyond work required by the two foundation models and Acceptance Model.
- Automatic STEP color-based machining selection.
- Multi-Setup, side-face, back-face, or automatic coordinate rotation output.
- Incremental-coordinate (`G91`) program generation.
- Full dynamic stock removal and holder/fixture swept-volume collision.
- Production machine-control UI, cycle control, alarm integration, and unattended DNC transfer.
- Fanuc acceptance parity with Siemens 840D.
