# Machine Profiles are snapshotted with generated output

Safe modal templates and postprocessor rules belong to a versioned Machine Profile rather than free-form per-operation text. Every Program Snapshot records the profile version and effective generation settings, preserving reproducibility when the active profile later changes while still allowing approved users to configure codes such as `G80`, units, WCS, and feed mode within validated constraints.
