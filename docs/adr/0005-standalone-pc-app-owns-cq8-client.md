# ADR 0005: Standalone PC application owns the CQ8 client

The only PC application and implementation base is `untitled/` (`CNEXT-CAM.exe`). It contains process-design, program-validation, and machine-operation workspaces and will directly own the future PC-side CQ8 TCP client. The product must not import, embed, launch, or depend on the separate `CNC_SYSTEM` project. CQ8 continues to own G-code interpretation, trajectory planning, real-time interpolation, PLC, IO, and control arbitration. This decision supersedes ADR 0004.
