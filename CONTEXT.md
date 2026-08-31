# CNEXT-CAM Domain

CNEXT-CAM converts selected machining intent from a STEP model into a validated machining job for the CQ8 control runtime, while retaining optional controller-specific export formats. This glossary defines the project language used by product, CAM, control integration, simulation, and persistence work.

## Language

**Setup**:
A confirmed machining orientation and work coordinate system used for one clamping of the workpiece.
_Avoid_: Front face, current face, station

**Recognized Feature**:
A geometric machining candidate identified from the source STEP model, such as a hole, slot, pocket, plane, or contour.
_Avoid_: Operation, toolpath, selected face

**Machining Geometry**:
The setup-relative, tool-center-safe geometric boundary derived from a Recognized Feature for toolpath generation.
_Avoid_: Raw feature, mesh bounds, display outline

**Machining Operation**:
A confirmed machining intent that combines Machining Geometry, a strategy, a tool, parameters, and an operation stage.
_Avoid_: Feature, G-code block, program

**Operation Proposal**:
A non-executable candidate that excludes infeasible choices and presents compatible machining strategies, tools, and parameter suggestions for a Recognized Feature until a user explicitly confirms one choice.
_Avoid_: Automatic operation, default toolpath, generated program

**Process Template**:
A versioned set of suggested cutting parameters for a user-selected material, machining strategy, and compatible tool range. Applying it copies editable values into an Operation Proposal without changing existing confirmed operations.
_Avoid_: Automatic process decision, live global defaults, material detection

**Machine Profile**:
The approved target-controller and machine-output policy, including supported commands, safe state requirements, naming rules, and translation behavior.
_Avoid_: Global settings, postprocessor name, machine UI

**Program Snapshot**:
An immutable record of generated CNC output and the exact operations, Machine Profile version, and generation settings that produced it.
_Avoid_: Live program, editor contents, operation list

**Execution Preview**:
The expanded motion and safety view derived exclusively from the final controller-targeted output, with source lines, operations, and tool positions kept traceable.
_Avoid_: Strategy preview, internal toolpath buffer, material-removal simulation

**Program Package**:
The complete controller-specific deliverable generated from a Program Snapshot. Its files and structure depend on the selected Machine Profile.
_Avoid_: Single G-code string, export file

**CQ8 Machining Job Package**:
The immutable, validated handoff from CNEXT-CAM to the CQ8 Control Runtime, tied to one Program Snapshot and one CQ8 Machine Profile.
_Avoid_: Raw serial send, live editor text, Siemens package

**CQ8 Control Runtime**:
The machine-facing execution boundary that owns CQ8 communication, machine state, interlocks, program loading, and real-time motion control.
_Avoid_: CNEXT-CAM, postprocessor, CAM simulator

**CQ8 Integration Port**:
The versioned handoff contract reserved by CNEXT-CAM for future delivery of validated machining jobs to the CQ8 Control Runtime.
_Avoid_: Live CQ8 connection, direct motion control, raw serial sender

**Acceptance Model**:
The committed production-representative STEP model used for final end-to-end acceptance after the smaller hole and slot regression models pass.
_Avoid_: Demo model, sample part
