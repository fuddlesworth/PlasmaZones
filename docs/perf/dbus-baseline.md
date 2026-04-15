# D-Bus Performance Baseline

Tracks before/after numbers for the refactor on branch `refactor/dbus-performance`.

## Running

```bash
cmake --build build --parallel $(nproc) --target bench_dbus_adaptors
./build/tests/unit/bench_dbus_adaptors -tickcounter
# or
ctest --test-dir build -R bench_dbus_adaptors --output-on-failure
```

`-tickcounter` gives cycle-accurate numbers; `-callgrind` writes a callgrind
profile. `-event` is the default (counts instructions). All three are useful —
pick whichever is available on the host.

Benchmarks live in `tests/unit/dbus/bench_dbus_adaptors.cpp`. They use
`StubSettings` + an in-memory `LayoutManager`, so there is no disk I/O in the
hot path and the numbers reflect pure adaptor + QJson serialization cost.

## Baseline (pre-refactor, branch base = v3 @ a3ee7c44)

Captured on the reference workstation (CachyOS, GCC 15.2, Qt 6.11.0, Unity build,
release) before any Phase 1 work landed. Numbers vary by host — what matters is
the ratio between runs on the same machine.

| Benchmark                                  | ms / call | Notes |
| ------------------------------------------ | --------- | ----- |
| `benchSetSetting_unchanged`                | 0.0010    | Same cost as changing — setter runs unconditionally |
| `benchSetSetting_changing`                 | 0.0010    | Reference path |
| `benchGetSettings_batch` (8 keys)          | 0.0074    | In-process, slower than N calls due to QVariantMap conversion — the batch win is marshal count, not CPU |
| `benchGetSetting_individual` (8 × 1 key)   | 0.0017    | |
| `benchSetLayoutHidden_toggle`              | 0.095     | Full layout JSON (~1KB here, 5–20KB in prod) re-serialized each call |
| `benchSetLayoutHidden_sameValue`           | 0.039     | Still pays full JSON cost — no value-equality guard |
| `benchGetLayout_cached`                    | 0.00063   | Cached-path baseline (Phase 1.3 must stay within noise) |

## Phase-by-phase expected wins

- **Phase 1.1 (value-equality guard):** `benchSetSetting_unchanged` drops by
  the cost of the setter lambda + `scheduleSave()` timer restart. Expected
  50–80% reduction on the unchanged path. The changing path must stay within
  noise of baseline.
- **Phase 1.2 (drop layoutListChanged on property mutations):**
  `benchSetLayoutHidden_toggle` drops one signal emission and one empty-arg
  marshal. Expected 10–20% reduction.
- **Phase 1.3 (cache invalidation):** `benchGetLayout_cached` stays flat;
  correctness change, no throughput impact.
- **Phase 4 (layoutPropertyChanged delta signal):** `benchSetLayoutHidden_toggle`
  drops the full JSON serialization + marshal of a 5–20KB string. Expected
  60–90% reduction.

## Post-refactor (branch tip)

Filled in after Phase 6.

| Benchmark                                  | Metric    | Value | Δ vs baseline |
| ------------------------------------------ | --------- | ----- | ------------- |
| `benchSetSetting_unchanged`                | ns / call | _TBD_ | _TBD_         |
| `benchSetSetting_changing`                 | ns / call | _TBD_ | _TBD_         |
| `benchGetSettings_batch` (8 keys)          | ns / call | _TBD_ | _TBD_         |
| `benchGetSetting_individual` (8 × 1 key)   | ns / call | _TBD_ | _TBD_         |
| `benchSetLayoutHidden_toggle`              | ns / call | _TBD_ | _TBD_         |
| `benchSetLayoutHidden_sameValue`           | ns / call | _TBD_ | _TBD_         |
| `benchGetLayout_cached`                    | ns / call | _TBD_ | _TBD_         |
