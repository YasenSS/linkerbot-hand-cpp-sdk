# Historical O30 HOP 0.0.4 acceptance

[English summary](README.en.md) | [Original Chinese record](README.md)

This record predates removal of O30 motion-time and torque-limit APIs. Its interface
and configuration statements apply only to the recorded SDK revision.
`verify_fixes.cc` is a standalone historical acceptance program, not a default CTest target.
Do not compile or run it as a current generic diagnostic without reviewing API changes.

The 2026-09-17 session used an O30 right hand, application 1.1.0, build timestamp
2026-09-14 10:53:01. Checks covered identity, fault/error history, tactile data and sparse
writes. The program could change the index fingertip target by at most 12/255 travel,
verify that the other 19 targets remained unchanged, then restore targets and sensor selection.
The recorded link used standard CAN FD without BRS, 1 Mbps, request ID `0x001`, reply `0x401`.

The original baseline passed default CTest 5/5 (480 communication cases, 15,173 assertions),
O30-specific offline regression (92 cases, 978 assertions), and 29 read-only hardware
cases (182 assertions). Full acceptance stopped before motion because nonzero faults
were present. Recorded raw `0x02` faults included thumb roll/yaw, index Root2 and thumb tip;
index yaw also showed temperature raw count 81. The actual cause was not independently
verified with external instruments. Earlier successful tactile/sparse-motion observations
were not relabeled as successful acceptance of this later baseline.

See the original record and `evidence-20260917` files for exact historical commands,
API changes and limitations. Use the [current hardware guide](../../device/hardware/README.md)
for maintained operating instructions. No new hardware validation is claimed by this summary.
