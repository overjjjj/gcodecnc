---
status: superseded by ADR-0004
---

# Siemens output is an MPF and SPF program package

Siemens 840D is the first production acceptance controller, and generated output will be a Program Package containing one main `.mpf` plus reusable `.spf` subprograms when machining logic repeats at least twice. The main program owns tools, spindle, coolant, WCS, and safe machine state; subprogram parameters are limited to geometry, position, depth, and cutting values so controller state remains auditable.
