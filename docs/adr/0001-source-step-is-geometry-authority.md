# Source STEP is the project geometry authority

CNEXT-CAM projects will store the source STEP path and SHA-256 fingerprint rather than embedding a second authoritative mesh in the `.cnext` file. Loading a project must re-import the matching STEP; a missing file blocks generation, while a changed fingerprint requires re-recognition and invalidates existing operations and Program Snapshots so old toolpaths cannot silently target changed geometry.
