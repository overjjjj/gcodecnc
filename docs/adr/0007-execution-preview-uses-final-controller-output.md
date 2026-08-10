# Execution preview uses final controller output

The first CAM acceptance validates and visualizes the final controller-targeted output rather than a separate strategy-internal toolpath. Cycles, macros, and subprograms are expanded for traceable motion review, and feature-bound, depth, rapid-move, tool-envelope, and stock-bottom violations block formal output; dynamic material removal, remaining-stock calculation, and full holder/fixture swept-volume simulation are deferred until the foundation models and Acceptance Model pass.
