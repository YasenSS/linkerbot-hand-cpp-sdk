# Historical hardware acceptance

[English](README.en.md) | [简体中文 / original records](README.md)

This index summarizes the existing Chinese reports. It does not translate raw logs or
claim that historical devices were retested for the current SDK. Each original report
retains its revisions, firmware, commands, failures and follow-up results.
`acceptance/<batch>/` identifies an external evidence archive, not a downloadable directory here.

## Model reports

| Model | Recorded coverage and remaining limits |
| --- | --- |
| [L6](l6/report.md) | 34 common Linux checks passed; Windows Debug PCAN and SLCAN Classic discovery, reads and single-joint movement/restoration recorded |
| [O6](o6/report.md) | 33 common Linux checks eventually passed; Windows Debug PCAN and SLCAN Classic discovery, reads and single-joint movement/restoration recorded |
| [L20 Lite](l20lite/report.md) | Linux joint arrival/coupling differences retained; Windows Debug PCAN and SLCAN Classic checks include joint 0 movement/restoration |
| [L20](l20/report.md) | 32 common checks passed; Windows Debug PCAN/SLCAN Classic recorded, with earlier fault and snapshot-expiry observations retained |
| [L30](l30/report.md) | 34 Linux checks passed with additional skipped cases; Windows Debug SLCAN FD recorded; previous pose-range and CLI failures remain documented |
| [O30](o30/report.md) | Common Linux API validation recorded; Windows Debug MiniCANFD/SLCAN FD connection, reads and single-joint movement/restoration recorded |

The [Windows procedure and detailed matrix](windows.md) is an original Chinese record.
Its 2026-10-19 adapter matrix covers Debug only: PCAN Classic for L6/O6/L20 Lite/L20,
SLCAN for all six models using the recorded CANable profiles, and MiniCANFD for O30.
L30/O30 PCAN FD and L30 MiniCANFD were not hardware-validated in that matrix.
Lawicel firmware, Release motion, all-joint Windows motion, USB hotplug and long-duration
load are not implied by those results. O30 MiniCANFD used `HCanbus.dll 3.3.103.0`;
older DLL failures are preserved in the original report.

A separate 2026-10-19 Windows Release engineering run recorded core CTest 143/143,
installed/extracted consumers 4/4, 219 public headers, ten documentation examples and
nine compatibility groups. These are historical suite counts, not present-day totals
or Release hardware certification.

## Current procedure and interpretation

Use the English [hardware test guide](../../tests/hand/device/hardware/README.md)
for current commands, opt-in switches and baseline requirements, and the
[compatibility reference](../en/reference/compatibility.md) for supported boundaries.
Only checks actually run successfully can be recorded as passes. Skipped tests, no matching
tests and unperformed motion are separate outcomes. A send receipt, device ACK, measured
position and physical restoration are distinct evidence.

The older [O30 HOP acceptance program](../../tests/hand/o30/hardware/README.md) is
summarized separately; it refers to an API revision before later feature removals.
New reports should record SDK/transport SHAs, platform/toolchain, device identity,
firmware/driver versions, exact commands, baseline provenance and limitations. Keep
private raw logs outside the repository and link only evidence actually available.
