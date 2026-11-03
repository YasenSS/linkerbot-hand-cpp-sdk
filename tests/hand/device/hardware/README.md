# HandDevice hardware tests

[English](README.md) | [简体中文](README.zh-CN.md)

These tests exercise `linkerbot::hand::HandDevice` against a real device. Model-specific
suites under `tests/hand/<model>/` are separate. O30 angle preparation additionally
reads the native original target, because the unified angle getter reports actual
positions; that preparation query sends no motion command.

## Prepare the connection

Configure the real interface before running tests. For L6, O6, L20 and L20 Lite Classic CAN:

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 fd off
sudo ip link set can0 up
```

For L30 CAN FD:

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 1000000 dbitrate 5000000 fd on
sudo ip link set can0 up
```

O30 uses standard CAN FD frames without BRS; match the adapter and firmware settings
in the [connection guide](../../../../docs/en/guides/usage.md).
Check the configured interface with `ip -details -statistics link show can0`.
After unplugging an adapter, restore its interface configuration before continuing.

Windows PCAN and MiniCANFD use `HAND_DEVICE_BACKEND=pcan|minicanfd`; SocketCAN remains
the Linux default. See [Windows adapters](../../../../docs/en/connections/windows-adapters.md)
and the [historical acceptance summary](../../../../docs/hardware-acceptance/README.en.md).

## Build and select tests

```bash
cmake --build build --target tests --parallel 4
./build/tests/tests --list-tests "[hardware][device]"
```

The unified suite selects a model through `HAND_DEVICE_MODEL`, not a `[l6]` or `[l30]` tag.
Use `HAND_DEVICE_SIDE` for Classic CAN, and `HAND_DEVICE_ADDRESS` /
`HAND_DEVICE_HOST_ADDRESS` for L30 node/host IDs. Set `CAN_INTERFACE` to the real interface.

The source files group identity, lifecycle, control, angles, parameters, telemetry,
polling, force, validation, command contracts, disconnect and load tests.
`hand_device_api_catalog.hpp` is the maintained method/parameter catalog. Listing tests
is read-only; a static method count is not evidence that every device passed every path.
Unsupported capabilities must raise `UnsupportedOperationError` explicitly.

For a configured L30:

```bash
export HAND_DEVICE_MODEL=l30 CAN_INTERFACE=can0
export HAND_DEVICE_ADDRESS=1 HAND_DEVICE_HOST_ADDRESS=0
./build/tests/tests "[hardware][device][read-only]"
./build/tests/tests "[hardware][device][validation]"
```

These groups do not write normal targets or configuration. Identity refresh and recovery
re-establish the SDK connection. Factory/shared-Session and fault-injection paths use the
selected adapter. O30 borrows the same Session for original-target queries; creating its
fixture already opens a Session and is not a zero-I/O constructor check.

## Opt-in write and manual groups

| Set to `1` | Scope / selection |
| --- | --- |
| `HAND_DEVICE_ENABLE_PARAMETER_TESTS` | Change, read and restore supported settings; `[parameters][write]` |
| `HAND_DEVICE_ENABLE_CONTROL_TESTS` | Clear faults, enable/disable; `[control][write]` |
| `HAND_DEVICE_ENABLE_SAVE_TESTS` | Save parameters; `[persistent]`; ACK does not prove persistence across power loss |
| `HAND_DEVICE_ENABLE_FAULT_TESTS` | Suppress replies, fail sends or block a driver; `[commands][fault-injection]` |
| `HAND_DEVICE_ENABLE_MOTION_TESTS` | Angle movement and command-contract writes of the initial valid pose |
| `HAND_DEVICE_ENABLE_DISCONNECT_TESTS` | Operator-assisted cable removal/reconnection; `[disconnect][manual]` |
| `HAND_DEVICE_ENABLE_LOAD_TESTS` | At least 600 seconds of read-only angle load; `[load][manual][read-only]` |
| `HAND_DEVICE_ENABLE_LIFECYCLE_STRESS` | At least 100 open/close cycles with recovery; `[lifecycle][manual][read-only]` |

Freeze load thresholds before a run and collect resource trends separately for lifecycle stress.
Parameter tests attempt restoration on normal and exceptional exits. Queryable settings
start from device values; command-only caches require operator-confirmed baselines,
otherwise the section is skipped. Do not invent baseline values.

L30 requires 17 comma-separated raw values for `HAND_DEVICE_BASELINE_SPEEDS` and
`HAND_DEVICE_BASELINE_CURRENT_LIMITS`. L20 requires 16 each and only confirms transport
receipts, because it cannot query those settings. O6 requires six each from the operator:
firmware V1.3.2's one-byte speed query can set speed to zero, so the unified getter uses
write-response data instead. It cannot reveal the initial speed before the first write.
L20 Lite requires ten each. Other baseline variables are listed in the API catalog.

Distinguish `SDK-command-cache`, `device-write-response`, `device-query`, and
`transport-receipt-only`. They do not establish the same fact. A newly established
baseline is not restoration of an unrecorded original setting. Historical L30/L20 Lite
sessions used raw speed 20 and current limit 60; these are recorded test values, not
recommended defaults. No EEPROM write is implied.

L6 V2.4.5 and L20 Lite V4.6.10 have recorded one-raw-unit rounding in certain settings.
The corresponding tests use steps of at least three and one-unit tolerance where specified.
For command-only settings, a second identical write may retrieve confirmation after the
first succeeds. L20 Lite's tested firmware exposes `fn` matrices but rejects unsupported
scalar fields and normal-force resultants through its capability contract.

```bash
HAND_DEVICE_ENABLE_FAULT_TESTS=1 HAND_DEVICE_ENABLE_MOTION_TESTS=1 \
  ./build/tests/tests "[hardware][device][commands]"
HAND_DEVICE_ENABLE_PARAMETER_TESTS=1 \
  ./build/tests/tests "[hardware][device][parameters][write]"
```

Command-contract tests retain a valid starting pose. O30 preserves original targets instead
of accumulating feedback error into targets. Target restoration and measured-position
restoration are checked independently. Injected transport faults are not physical cable tests.
Saving parameters still requires physical persistence checks; L30 explicitly rejects unsupported saving.

## Run an angle session

Example: L6 right hand, default address `0x27`, interface `can0`:

```bash
export HAND_DEVICE_MODEL=l6 HAND_DEVICE_SIDE=right
export HAND_DEVICE_ADDRESS=0x27 CAN_INTERFACE=can0
unset HAND_DEVICE_HOST_ADDRESS HAND_DEVICE_ANGLE_JOINT HAND_DEVICE_ANGLE_NORMALIZE_START
export HAND_DEVICE_ANGLE_STEP_PERCENT=5
export HAND_DEVICE_ANGLE_TOLERANCE_PERCENT=2
export HAND_DEVICE_ANGLE_SETTLE_MS=5000
./build/tests/tests "[hardware][device][read-only]"
./build/tests/tests "[hardware][device][validation]"
```

After confirming emergency stop, clear travel and the correct device selection:

```bash
HAND_DEVICE_ENABLE_MOTION_TESTS=1 \
  ./build/tests/tests "[hardware][device][angles]" --order decl
```

Use `--list-tests "[hardware][device][angles]"` to inspect the actual suite first.
Without `HAND_DEVICE_ANGLE_JOINT`, all joints are selected. For only the combined case,
select `"HandDevice hardware full angle acceptance"`.
O30 single-joint preparation reads all 20 targets and preserves the others; a target/
feedback mismatch over 10% fails before motion. Failure to reach the target remains a failure.
L30 uses its actual node/host IDs and no Classic hand-side setting.

## Physical disconnect

```bash
export HAND_DEVICE_DISCONNECT_SIGNAL=/tmp/hand-device-disconnect.signal
printf 'ready\n' > "$HAND_DEVICE_DISCONNECT_SIGNAL"
HAND_DEVICE_ENABLE_DISCONNECT_TESTS=1 \
  ./build/tests/tests "HandDevice hardware physical disconnect and reconnect"
```

In another terminal, only after the indicated physical action:

```bash
printf 'disconnected\n' > /tmp/hand-device-disconnect.signal
# Reconnect the same device and restore the CAN configuration first.
printf 'reconnected\n' > /tmp/hand-device-disconnect.signal
```

Each operator step defaults to 300 seconds; `HAND_DEVICE_DISCONNECT_WAIT_MS` accepts
1000–1800000 ms and changes only operator waiting, not communication deadlines.
The marker does not replace checks of read failure, successful recovery, identity and
new-generation feedback. This case sends no motion target.

## Experimental native SLCAN

The transport build must expose `LINKERBOT_COMM_SLCAN_FD=1`. Explicitly select the
firmware profile and port; native serial access does not use `slcand` or create `can0`.
For example, an O30 read-only query:

```bash
HAND_DEVICE_MODEL=o30 HAND_DEVICE_SIDE=right HAND_DEVICE_ADDRESS=1 \
HAND_DEVICE_BACKEND=slcan HAND_DEVICE_CHANNEL=/dev/ttyACM0 \
HAND_DEVICE_SLCAN_PROTOCOL=canable-fd HAND_DEVICE_SERIAL_BAUD=115200 \
HAND_DEVICE_SERIAL_TIMEOUT_MS=500 HAND_DEVICE_BITRATE=1000000 \
HAND_DEVICE_DATA_BITRATE=5000000 \
./build/tests/tests "HandDevice hardware angle readback"
```

Adjust model/IDs for L30; omit data bitrate for Classic CAN. Lawicel firmware uses
`lawicel-classic`; CANable firmware uses `canable-fd`. Optional startup delay:
`HAND_DEVICE_SERIAL_STARTUP_MS=100`. Windows uses the same variables with the real COM port;
Linux requires serial permissions. CANable has no serial ACK: a successful write proves
serial-driver acceptance only. Use device feedback for acceptance claims.

## Record the result

`All tests passed` describes executed checks. Skips and empty selections do not count as
hardware passes. Record the revision, device, firmware, adapter, commands, failures,
unperformed checks, manual observations and restoration outcome in the model's
historical report. Keep raw private captures outside the repository.
Never add motion tests to default offline CTest. Files such as `test_device.cpp` outside
this hardware directory remain FakeTransport tests.
