# O30i protocol and test coverage

[English](README.md) | [简体中文](README.zh-CN.md)

The C++ namespace remains `linkerbot::hand::o30`; the device protocol is also named O30i.
This guide documents O30 behavior, not capabilities of other models.

## Mapping and direction

The original mapping work referenced private/local protocol documents that are not
part of this repository. The maintained public behavior is represented by
`kJointSemantics`, protocol code, test vectors and recorded hardware observations.
HOP 0.0.3 and 0.0.4 differ; do not infer 0.0.4 behavior from the older protocol alone.

Public percentages follow the Python unified interface: flexion axes use `0% = FF`
and `100% = 00`; other axes use `0% = 00` and `100% = FF`, for both hands. Percentages
are not angular units. The table describes increasing raw values, not increasing percentages.

| SDK indices | SI | Physical axis | Raw 00 → FF |
| --- | --- | --- | --- |
| 0 | 00 | `thumb_rotation`, protocol `roll_0` | Away from → toward index finger |
| 1 | 05 | `thumb_roll`, protocol `yaw_0` | Parallel → perpendicular to palm |
| 2–5 | 06–09 | Index, middle, ring, little lateral axes | Toward → away from thumb |
| 6–10 | 0A–0E | Five Root1 axes | Straight → bent |
| 11–14 | 10–13 | Four Root2 axes | Straight → bent |
| 15–19 | 14–18 | Five TIP axes | Straight → bent |

Only 20 of 36 slots are mounted. Full writes use ranges `00/1`, `05/10`, `10/9`,
skipping unmounted slots. Raw interfaces do not reverse values. The first two names
are `thumb_rotation` and `thumb_roll`; their indices remain unchanged, with
`kThumbRotationSi = 0x00` and `kThumbRollSi = 0x05`. To preserve raw targets saved
with the old increasing flexion mapping, convert `p` to `100 - p`.
Opening sets only flexion to 100%, preserving rotation, roll and lateral axes;
zeroing every axis is not a generic open pose.

## Protocol coverage

| Area | Contract covered offline |
| --- | --- |
| MI01 angles | Twenty axes, percentage setters, actual/target values and unchanged reserved/non-target slots |
| MI30 sparse writes | 0.0.3 finger-major IDs; 0.0.4 uses MI01 single-slot writes to avoid observed cross-joint behavior |
| MI02–07, 09–0B | Speed, acceleration, current, voltage, temperature, stall settings, snapshots and malformed lengths; MI06 retains actual torque reads only |
| Current writes | CTRL04 for 0.0.3, CTRL84 for 0.0.4; reject version-dependent operations when version is unknown |
| Faults | MI0D for 0.0.3, MI0C for 0.0.4; preserve old raw bytes instead of applying newer bit definitions |
| MI20, MI21–26 | Little-endian halfword angles/PID, multipart reads, 20/36-slot projection and invalid enums |
| MI31–34 tactile | Versioned metadata; 0.0.4 10×7/72-byte descriptors and 255/256/256-byte banks; 0.0.3 boundary handling; no data reads for NO_SENSOR |
| MI35 configuration | Field/ID validation; accept old/new-address ACK and switch client address only after success, invalidating version cache |
| MI36–3A presets | Five preset reads, raw writes and non-trigger fields; raw writes include trigger bytes |
| MI41 identity | 225/249-byte layouts, version, build time, side and short-response rejection |
| MI42 services | Fourteen status fields, SI, lengths, little-endian values and setters; reject short replies |
| MI4E units | Separate 16-byte unit text/decimal byte; 36 range entries and serialized select/query |
| MI4F errors | Preserve actual 15/16-byte history length; explicit reads are not treated as error replies |
| Transport/lifecycle | Standard FD frames, SI/EDL bounds, ordering, missing/conflicting fragments, unrelated frames, timeout, close, polling events and snapshots |

Expected wire data uses independent byte constants/builders, not the SDK encoder.
Relevant suites include `test_measured_alignment_comm.cpp`, `test_runtime_contract_comm.cpp`,
`test_metadata_contract_comm.cpp`, `test_wire_contract_comm.cpp` and `test_lifecycle_contract_comm.cpp`.

Enable masks did not work on tested 0.0.3 firmware; global enable is unverified on 0.0.4.
Enable and fault-clear operations reject unsupported behavior instead of inventing frames.
For 0.0.3, fault detection retains nonzero raw status without guessing its cause; motion
checks reject nonzero raw fault bytes. Some mapping operations receive no response or
protocol errors. Version 0.0.3 tactile data over 256 bytes has no verified bank layout
and is rejected. Offline encoding/PID/configuration/service/preset coverage is not
confirmation that firmware accepts or executes every write.
If an ID-change ACK is lost, the client keeps the old address but the device may have
changed; inspect its address before continuing.

## Run tests

```bash
cmake --build build --target tests protocol-tests --parallel 4
./build/tests/tests '[comm][o30]' --reporter compact
ctest --test-dir build --output-on-failure
```

`[comm]` uses FakeTransport and does not access CAN hardware. Historical counts in
[the original notes](README.zh-CN.md) refer to their recorded revision; use the current
runner output for current totals.

For a configured real FD interface, default `CAN_INTERFACE=can0` and `O30_REQUEST_ID=0x001`:

```bash
./build/tests/tests '[o30]~[comm]~[motion]~[configuration]~[interactive]' --list-tests
CAN_INTERFACE=can0 O30_REQUEST_ID=0x001 ./build/tests/tests '[o30]~[comm]~[motion]~[configuration]~[interactive]'
```

Interactive angle tests deliberately use Chinese operator prompts. Review those prompts
before running motion. Start with opening/closing or lateral motion:

```bash
./build/tests/tests '[o30][angle][open_close]~[comm]'
./build/tests/tests '[o30][angle][yaw]~[comm]'
./build/tests/tests '[o30][angle][interactive]~[comm]'
```

Enter starts a step; `y` passes, `n` fails, `s` leaves it incomplete, and `q` exits.
Invalid input is retried. Skip/exit/EOF never count as passes. Failure or exit stops
subsequent motion; restoration is attempted after motion unless a fault prevents writes.
Preflight is read-only. Reports record joints, directions and coupling in
`tests/reports/o30_angle_*.json`. The historical 2026-09-17 preflight failure is preserved
in the original notes and does not establish hardware acceptance.

Speed tests save/restore speed, acceleration and pose. One requires nonzero measured
speed during motion; another compares the same 100%→30% travel at settings 20 and 200,
requiring the higher setting to be at least 30% faster. A 20-second arrival timeout
fails instead of being used as a valid timing measurement. Interactive failures stop
remaining motion in the process. `[configuration]` writes settings; `[interactive]`
requires operator input. Tactile interaction uses a verified little-endian uint16 FnS
decoder and expects increased force on pressing; missing sensors/decoders are skipped.
