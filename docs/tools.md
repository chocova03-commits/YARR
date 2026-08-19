# Tool Reference

YARR builds a large number of small, standalone command-line binaries in addition to
``scanConsole``. This page is a one-line inventory of every application built by
default, grouped by where its source lives. Tools that already have dedicated
documentation link to that page instead of repeating it here.

## Generic (`src/tools`)

| Tool | Description |
|------|-------------|
| ``scanConsole`` | Main scan-running application, see [ScanConsole Command](scanconsole.md) |
| ``analysis_test`` | CLI entry point for the analysis console, see [AnalysisConsole](analysisconsole.md) |
| ``createConfig`` | Generate a default chip configuration JSON for a given chip type |
| ``dbAccessor`` | Command-line client for uploading to / querying the Local DB (MongoDB) |
| ``hw_read_test`` | Direct read from a hardware controller channel to a file, without a scan |
| ``hw_write_test`` | Direct write of packet data to a hardware controller channel, without a scan |
| ``makeFei4PixelModelCfg`` | Generate an FE-I4 pixel model configuration used to simulate scan data |
| ``makeRd53aPixelModelCfg`` | Generate an RD53A pixel model configuration used to simulate scan data |
| ``replot`` | Replot a previously saved ``Histo1d``/``Histo2d`` JSON output file |
| ``scurve_test`` | Standalone closure test of the s-curve fit routine, no hardware needed |
| ``test_json`` | Validate/parse a connectivity, controller, front-end, or scan config JSON without touching hardware |
| ``dumpRawDataToText`` | Dump the raw event data blocks of one or more scan output files as human-readable text |
| ``checkTriggerTags`` | Check trigger-tag continuity/ordering in raw scan output files |
| ``analyseRawData`` | Run analysis/histogramming on saved raw data files without a live scan |
| ``write-register`` | Write a named chip register directly, without running a scan |
| ``read-register`` | Read a named chip register directly, without running a scan |
| ``read-adc`` | Read a chip ADC/Vmux value via the register interface |
| ``reset-all`` | Send a global reset to all front-ends on a control segment (tx line) |
| ``revert-chip-configs`` | Restore chip configs to the state recorded before (or after) a given scan run |

``write-register``, ``read-register``, ``read-adc``, ``reset-all`` and ``revert-chip-configs``
are only built on non-ARM targets.

## FE-I4 (`src/libFei4/app`)

| Tool | Description |
|------|-------------|
| ``fei4ReadRegister`` | Read an FE-I4 register directly over the SPEC controller |

## RD53A (`src/libRd53a/app`)

| Tool | Description |
|------|-------------|
| ``rd53aPlotTdac`` | Build TDAC distribution/map histograms from a saved pixel config JSON |
| ``rd53aReadRegister`` | Read an RD53A register directly over the SPEC controller |
| ``rd53aReadRingosc`` | Read RD53A ring-oscillator registers directly over the SPEC controller |

## RD53B / ITkPix v1 (`src/libRd53b/app`)

| Tool | Description |
|------|-------------|
| ``rd53bConnectivityScan`` | Scan Tx/Rx channels to find and identify connected chips, see [RD53 common info](rd53.md) |
| ``rd53bColumnTest`` | Check for stuck/faulty core columns |
| ``rd53bComTest`` | Basic RD53B communication/configuration test independent of a full scan |
| ``rd53bDataMergingCheck`` | Verify RD53B data-merging/lane configuration |
| ``rd53bDataMergingCheck_2to1`` | Verify RD53B 2:1 lane-merging configuration |
| ``rd53bDataMergingCheck_4to1`` | Verify RD53B 4:1 lane-merging configuration |
| ``rd53bDecodeTool`` | Decode raw RD53B hit-data blocks from a binary dump |
| ``rd53bEfuseCheck`` | Read and check the RD53B e-fuse chip serial-number data |
| ``rd53bReadRingosc`` | Send a global pulse and read ring-oscillator registers only, without a full configuration (Module QC) |
| ``rd53bRegSEETest`` | Repeatedly write/read back RD53B registers to detect single-event-effect (SEE) bit flips |
| ``rd53bSendGlobalPulse`` | Send a configurable global pulse to a front-end without running a scan |
| ``rd53bTest`` | Basic RD53B configuration/communication test |

## RD53C / ITkPix v2 (`src/libItkpixv2/app`)

| Tool | Description |
|------|-------------|
| ``Itkpixv2ConnectivityScan`` | Scan Tx/Rx channels to find and identify connected chips, see [RD53 common info](rd53.md) |
| ``Itkpixv2CheckPulse`` | Send a global pulse to two SCCs and check communication status (data-merging test) |
| ``Itkpixv2ColumnTest`` | Check for stuck/faulty core columns |
| ``Itkpixv2EncodeRawData`` | Re-encode decoded raw data back into ITkPixV2 event data words |
| ``Itkpixv2ReadRingosc`` | Send a global pulse and read ring-oscillator registers only, without a full configuration (Module QC) |

## SPEC (`src/libSpec/app`)

| Tool | Description |
|------|-------------|
| ``eyeDiagram`` | Scan the RX delay setting and produce an eye diagram, see [RD53 common info](rd53.md) |
| ``specComTest`` | Basic SPEC card communication test, see [Troubleshooting](troubleshooting.md) |
| ``specReadWriteReg`` | Read or write a raw SPEC firmware register, see [PCIe Firmware/Hardware](fw_guide.md) |
| ``specSoftReset`` | Issue a soft reset to the SPEC firmware, see [Firmware Trigger Tagging Guide](trigger_tagging.md) |
| ``config-GBT-FMC`` | One-time I2C configuration of the GBT-FMC clock chips (Si534x/LMK61E2) on a SPEC card |
| ``dataMergingCheck`` | Verify SPEC-side data-merging/lane configuration |
| ``specBenchmarkDma`` | Measure SPEC PCIe DMA transfer throughput |
| ``specBenchmarkSingle`` | Measure SPEC PCIe single-word transfer throughput |
| ``specErrorCheckDma`` | Soak-test SPEC PCIe DMA transfers for bit errors |
| ``specErrorCheckSingle`` | Soak-test SPEC PCIe single-word transfers for bit errors |
| ``switchLPM`` | Toggle low-power mode / AC-coupling signal generation on SPEC TX channels |

## FelixClient (`src/libFelixClient/app`)

See [FelixClient](felixclient.md) for ``elinkConfig``, ``felixRegister``,
``felix_client_bridge``, ``readWriteLpGBTRegister`` and ``testFelixClient``.

## Star (`src/libStar/app`)

| Tool | Description |
|------|-------------|
| ``benchmark_data_processor_star`` | Benchmark the Star raw-data processor's decode throughput |

See [Star](star.md) for ``test_star``, ``lcbFastCommand``, ``lcbL0a``,
``lcbRegisterCommand``, ``makeLCBSequence``, ``printStarSubRegisters`` and
``starFelixCommands``.
