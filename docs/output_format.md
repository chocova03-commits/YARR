# Scan Output Format

Each scan produces a timestamped directory under `data/` and a `last_scan` symlink pointing to it.

## Directory layout

```
data/
├── last_scan -> 000042_std_digitalscan/   # symlink to most recent scan
└── 000042_std_digitalscan/
    ├── scanLog.json                        # full run record (configs + metadata)
    ├── std_digitalscan.json                # copy of the scan config used
    ├── 0x10AA6.json.before                 # chip config before the scan
    ├── 0x10AA6.json.after                  # chip config after the scan
    ├── 0x10AA6_OccupancyMap.json           # per-chip histogram output
    ├── 0x10AA6_EnMask.json
    └── ...
```

The directory name is `<run_number>_<scan_name>`, where the run number is a zero-padded 6-digit integer incremented from `$HOME/.yarr/runCounter`.

## Run counter

The global run counter is stored as a plain integer in `$HOME/.yarr/runCounter`. It is incremented automatically at the start of each scan. To reset it (e.g. for reproducible test output):

```bash
truncate $HOME/.yarr/runCounter --size=0
```

The next scan will then start at run number `000001`.

## scanLog.json

`scanLog.json` records the full configuration of the run and is suitable for reproducing it exactly:

```
{
  "runNumber": 42,
  "testType": "std_digitalscan",  # scan name; the scan config itself is
                                   # saved separately as std_digitalscan.json
  "ctrlCfg": { ... },              # controller config as used
  "connectivity": [ ... ],         # connectivity config as used
  "ctrlStatus": { ... },           # hardware status before the scan
  "ctrlPostScanStatus": { ... },   # hardware status after the scan
  "stopwatch": { ... },            # timing breakdown (config/scan/processing/analysis), in ms
  "exec": "...",                   # the exact command line used to invoke scanConsole
  "yarr_version": { ... }          # YARR version info
}
```

You can re-run a scan from the log using JSON fragment references:

```bash
bin/scanConsole \
  -r data/last_scan/scanLog.json#/ctrlCfg \
  -c data/last_scan/scanLog.json#/connectivity/0 \
  -s path/to/scan.json
```

## File naming scheme

Histogram output filenames (`.json`, `.dat`, and plot images) are built by
`HistogramBase::createFilename()` (`src/libYarr/include/HistogramBase.h`) as:

```
<dir><prefix>_<name>[_<loop-position>].<ext>
```

- `prefix` is usually the chip serial (e.g. `0x10AA6`), or `LoopPlot` for
  loop-level histograms.
- `name` is the histogram's plain identifier, e.g. `OccupancyMap`,
  `ThresholdMap`. It never contains loop-position digits.
- `<loop-position>`, if present, is a **`-`-separated** list of loop
  iteration values (e.g. `0-1-2`), produced by `LoopStatus::toString()`. It
  is only appended when at least one loop is "enabled" for that histogram
  (see [Loop position identification](#loop-position-identification-uid)
  below); it is omitted entirely otherwise, along with its leading `_`.

So a full example is `0x10AA6_ThresholdMap_0-2.json`: `_` always separates
the top-level fields (`prefix`, `name`, loop-position block), while `-`
is only ever used *inside* the loop-position block to separate the
per-loop values. This is deliberate and consistent throughout the
codebase — code that parses histogram names (e.g.
`NPointGain::processHistogram` splitting off a `ThresholdMap`/`NoiseMap`
prefix) does so on `_`, never on `-`, since `-` may legitimately appear
multiple times inside the loop-position block and is not a reliable
top-level separator. If you are writing a downstream consumer that parses
these filenames or the `"Name"` field in the JSON (see below), split on
the *first* `_` for `<prefix>` vs. the rest, and treat any trailing
`_<digits>(-<digits>)*` block as the optional loop-position suffix.

## Loop position identification (UID)

During a scan, `LoopStatusMaster` (`src/libYarr/include/LoopStatus.h`)
tracks the current iteration value of every loop in the scan's loop stack.
Each time a histogram is produced, an immutable snapshot is taken via
`LoopStatusMaster::record()`, returning a `LoopStatus` that gets attached
to the histogram.

Analyses use this `LoopStatus` in two ways:

- **As a map key** (`LoopStatus::UID`, a fixed-size
  `std::array<unsigned, 8>`), via `uniqueID()`, to group histograms
  belonging to the same outer-loop iteration (e.g. all occupancy maps for
  a given threshold/injection setting) while iterating with a
  `std::map<LoopStatus::UID, ...>`.
- **As a filename/display string** (`toString()`), a `-`-separated list of
  the same enabled loop values, used to build the on-disk filename
  described above.

Both are computed from the same underlying per-loop values, filtered by
which loops are currently "enabled" for that analysis
(`setEnabledLoops()`/`setDisabledLoops()`, called once in each analysis's
`init()`). An analysis typically enables all its own parameter loops and
disables mask/trigger/data loops (which don't distinguish histograms) and
loops it is itself scanning over (e.g. `ScurveFitter` disables its Vcal
loop, since that's the axis being fit, not a grouping key).

Note the two representations are not simple string/array views of each
other: `uniqueID()` **zero-fills** disabled loop slots (keeping the array
fixed-size and index-stable), while `toString()` **omits** disabled loops
from the string entirely. This means a disabled loop and an enabled loop
whose current value happens to be `0` both show up as `0` at that index in
the `UID` — code that branches on a specific UID slot being `0` (e.g.
`ScurveFitter`'s outer-loop feedback bookkeeping) relies on knowing that
slot is always enabled in that analysis; this is a pre-existing pattern
inherited from before the UID refactor, not something new.

## Histogram JSON format

Each histogram file is named `<chip_serial>_<histogram_name>.json`. The format is:

```json
{
  "Name":      "OccupancyMap",
  "Type":      "Histo2d",
  "Entries":   153600,
  "Overflow":  0.0,
  "Underflow": 0.0,
  "x": { "Bins": 400, "Low": 0.5, "High": 400.5, "AxisTitle": "Column" },
  "y": { "Bins": 384, "Low": 0.5, "High": 384.5, "AxisTitle": "Row" },
  "z": { "AxisTitle": "Hits" },
  "Data": [
    [100.0, 100.0, ...],   // row 0, one float per column
    [100.0, 100.0, ...],   // row 1
    ...
  ]
}
```

`Data` is a 2D array of **floats** indexed `[row][col]`. For `Histo1d` the `Data` key is a flat array and the `y`/`z` axis keys are absent.

Note that `"Name"` is always the plain histogram name — it never carries a
loop-position suffix, unlike the filename. The raw per-loop iteration
values (not the UID or the `-`-joined display string) are separately
recorded in a `"loopStatus"` array, one entry per loop in the scan's loop
stack, e.g. `"loopStatus": [0, 2]`. This is a third, independent
representation of loop position alongside the filename suffix and the
in-memory `UID` — all three derive from the same `LoopStatus` snapshot but
should not be assumed to match in length or content, since the filename
and `UID` are filtered by which loops are "enabled" for a given analysis
while `"loopStatus"` always records every loop, enabled or not.

## Chip config snapshots

`<chip_serial>.json.before` and `<chip_serial>.json.after` capture the full chip register state immediately before and after the scan. Comparing the two shows any register changes applied by the scan (e.g. tuned TDAC values).
