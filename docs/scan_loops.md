# Scan Loops

The core of the scan engine is the scan loop action.

These form a hierarchical scan structure, with the actions of inner loops
proceeding at least once for an iteration of the outer loop.

There are some special loops. The job of the data loop is to provide data
to the data processor, which feeds the histogrammers.

The trigger loop is provided by the FrontEnd specific library and allows
building and executing a trigger loop which generates data.

## StdDataGatherer

This loop gathers data until a time limit is reached, or until a signal is
received.

## StdDataLoop

The normal data loop collects data until the appropriate number of events
have been collected.

### Archiving raw data

Setting `"archiveRawData": true` in the scan's `"processor"` block saves the
raw, pre-decode hardware words of every FE to `<feName>_rawdata.bin` in the
scan's output directory:

```json
"scan": {
  "processor": { "archiveRawData": true },
  ...
}
```

Both `StdDataLoop` and `StdDataGatherer` then also pass a copy of each
`RawDataContainer` to a `RawDataArchiver`
(`src/libYarr/RawDataArchiver.cpp`), which writes it to disk on its own
thread. The normal decode path is unaffected, and nothing is copied when the
option is off. This is useful for diagnosing decode failures against what the
hardware actually sent, rather than only the decoded result.

Use the `dumpRawDataArchive` tool (see [tools.md](tools.md)) to inspect the
resulting file offline, or the ITkPixV2-specific `itkpixv2DecodeTool` to
decode it into per-pixel hits.

## StdParameterLoop

The parameter loop provides a way to iterate over any variable.

## StdRepeater

The repeater repeats its contents a fixed number of times. The main use case
is for time evolution.
