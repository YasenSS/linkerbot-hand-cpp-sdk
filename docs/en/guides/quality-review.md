# Quality baseline review

[English](quality-review.md) | [简体中文](../../zh/guides/quality-review.md)

The [diagnostic ledger](../../quality-baseline-2026-11-03.csv) preserves all 717 findings from
[the completed baseline run](https://github.com/soulteary/linkerbot-hand-cpp-sdk/actions/runs/41663452602),
PR head `26912797fe297bafdd8be40cab09e2a847221c60`. Entries describe treatment, not a claim
that every diagnostic has been eliminated. Remaining findings stay visible in new reports.

## Runtime fixes and evidence

`ExecutionGate` formerly allocated a shared-pointer control block while holding its mutex.
Allocation failure invokes its deleter, which tried to lock the same mutex. A reduced
fault-injection reproduction stalled. The lease is now allocated before locking and
activated only after admission; inactive destruction cannot release another caller's lease.
Tests cover failed allocation with an incumbent, timeout, cancellation, invalid generation,
subsequent admission and the gate's lifetime while a lease exists.

The installed consumer now catches exceptions across its entire run, including discovery,
construction and I/O. Joint-name helpers explicitly validate indices while preserving
`std::out_of_range`. Request submission and waiting are separate statements with unchanged
ownership and deadline semantics.

## Verified analyzer limitations

A minimal `Progress { std::any value; }` returning an owning vector reproduces
`clang-analyzer-cplusplus.NewDeleteLeaks` on Clang 18/libstdc++ 13. The same program
passes ASan/UBSan. The permanent ownership regression checks that payloads survive copies
and are released with the final owner. Narrow annotations document those return sites.

Fixed-underlying-type enums permit unnamed representable values, including bit masks and
invalid-input test values. Clang 18 reports `EnumCastOutOfRange` for a reduced `uint8_t`
bit-mask example too. See [the standard conversion rule](https://eel.is/c++draft/expr.static.cast).
Annotations preserve the enum values, public types and negative tests.

The public polling aggregate is not reordered to satisfy the padding suggestion: member
layout and positional initialization are compatibility concerns. RAII cleanup flags and
snapshots used by later assertions are retained. Destructor exception annotations explain
why explicit close/stop reports errors while destruction cannot throw.

## Test access and remaining work

The checked optional helper returns an owned value and throws if disengaged, including
for temporary and nested optionals. Existing value assertions remain in place; hardware
checks are compiled and analyzed but require real devices to execute.

Remaining findings include private/public parameter-domain clarity, guarded runtime optional
accesses, numeric conversions and non-destructor catch policies. These require individual
review; changing a public signature or hiding a restoration failure for a better score is
not acceptable. Future quality gates should be based on the completed report, with errors
resolved first and warning reduction measured by affected files as well as diagnostic count.
The rule groups, source scope and scoring formula are unchanged.
