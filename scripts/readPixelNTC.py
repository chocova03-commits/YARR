#! /usr/bin/env python3
"""Read the external NTC temperature of one or more chips via read-adc.

Measures Vmux2 (Vntc), Vmux30 (analog ground), Imux9 (Intc), and Imux63
(digital ground) through the read-adc tool, then converts them to a
temperature using each chip's own NtcCalPar (falling back to the YARR
default if a chip's config doesn't define one).
"""
from __future__ import annotations

import argparse
import json
import logging
import math
import os
import shutil
import subprocess
import sys
from pathlib import Path

log = logging.getLogger(__name__)
logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

V_MUX_NTC = 2
V_MUX_GND = 30
I_MUX_NTC = 9
I_MUX_GND = 63

DEFAULT_NTC_CAL_PAR = [
    0.0007488999981433153,
    0.0002769000129774213,
    7.059500006789676e-08,
]


def default_read_adc_bin() -> Path:
    return Path(__file__).resolve().parent.parent / "bin" / "read-adc"


def resolve_read_adc_bin(user_path: str | None) -> Path:
    if user_path:
        path = Path(user_path).resolve()
        if not path.is_file():
            sys.exit(f"ERROR: read-adc binary not found at {path}")
        log.debug("Using user-specified read-adc binary: %s", path)
        return path

    path = default_read_adc_bin()
    if path.is_file():
        log.debug("Using default read-adc binary: %s", path)
        return path

    which = shutil.which("read-adc")
    if which:
        log.debug("Using read-adc binary found on $PATH: %s", which)
        return Path(which)

    sys.exit(
        f"ERROR: Could not find read-adc binary (looked in {path} and $PATH). "
        "Use --read-adc-bin to specify it explicitly."
    )


def resolve_chip_config_path(chip: dict, connectivity_file: Path) -> Path | None:
    config = chip["config"]
    path_type = chip.get("path", "relToExec")
    if path_type == "relToCon":
        return connectivity_file.parent / config
    if path_type == "abs":
        return Path(config)
    if path_type == "relToYarrPath":
        yarr_path = os.environ.get("YARR_CONFIG_PATH", "")
        return Path(yarr_path) / config
    if path_type == "relToExec":
        return Path(config)
    log.warning("Chip config path type '%s' is not supported by this script, skipping NtcCalPar lookup", path_type)
    return None


def get_chip_list(connectivity_file: Path) -> list[dict]:
    """Return enabled chips from the connectivity file, in file order.

    Each entry has: index, name, ntc_cal_par.
    """
    data = json.loads(connectivity_file.read_text())
    chips = []
    for idx, chip in enumerate(data["chips"]):
        if chip.get("enable", 1) == 0:
            continue

        ntc_cal_par = DEFAULT_NTC_CAL_PAR
        config_path = resolve_chip_config_path(chip, connectivity_file)
        name = None
        if config_path is not None and config_path.is_file():
            chip_config = json.loads(config_path.read_text())
            chip_type = next(iter(chip_config))
            params = chip_config[chip_type]["Parameter"]
            name = params.get("Name")
            if "NtcCalPar" in params:
                ntc_cal_par = params["NtcCalPar"]
            else:
                log.warning(
                    "No NtcCalPar found in %s, using default NTC parameters for this chip",
                    config_path,
                )
        else:
            log.warning(
                "Could not read chip config for chip at index %d, using default NTC parameters",
                idx,
            )

        log.debug(
            "Chip at index %d: name=%s, config=%s, NtcCalPar=%s",
            idx, name, config_path, ntc_cal_par,
        )
        chips.append({"index": idx, "name": name, "ntc_cal_par": ntc_cal_par})

    return chips


def run_read_adc(
    read_adc_bin: Path,
    hw_controller_file: str,
    connectivity_file: str,
    monitor: int,
    *,
    measure_current: bool,
    chip_indices: list[int],
    chip_names: list[str],
    shared_vmux: int | None,
) -> list[float]:
    # NB: read-adc's own -d debug flag writes trace output to stdout (the
    # same stream the measured value is printed to), which would corrupt
    # parsing below, so it is deliberately not passed through here.
    cmd = [str(read_adc_bin), "-r", hw_controller_file, "-c", connectivity_file]
    for idx in chip_indices:
        cmd += ["-i", str(idx)]
    for name in chip_names:
        cmd += ["-n", name]
    if shared_vmux is not None:
        cmd += ["-s", str(shared_vmux)]
    if measure_current:
        cmd.append("-I")
    cmd.append(str(monitor))

    label = f"Imux{monitor}" if measure_current else f"Vmux{monitor}"
    log.debug("Measuring %s, running: %s", label, " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.stderr.strip():
        log.debug("read-adc stderr for %s: %s", label, result.stderr.strip())
    if result.returncode != 0:
        log.error("read-adc failed (exit %d): %s", result.returncode, result.stderr.strip())
        sys.exit(1)

    values = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        values.append(float(line.split()[0]))
    log.debug("%s raw values: %s", label, values)
    return values


def calc_temp_ntc(vntc: float, intc: float, ntc_cal_par: list[float]) -> float:
    rntc = vntc / intc
    a, b, c = ntc_cal_par
    log_r = math.log(rntc)
    return 1 / (a + b * log_r + c * (log_r**3)) - 273.15


def main():
    parser = argparse.ArgumentParser(
        description="Read external NTC temperature of one or more chips via read-adc."
    )
    parser.add_argument("-r", "--hw-controller-file", required=True, help="Hardware controller JSON file path")
    parser.add_argument("-c", "--connectivity-file", required=True, help="Input connectivity JSON file path")
    parser.add_argument(
        "-i",
        "--chip-index",
        type=int,
        action="append",
        default=[],
        help="Position of chip in connectivity file chips list, starting from 0 (default: all chips). Can be given multiple times.",
    )
    parser.add_argument(
        "-n",
        "--chip-name",
        action="append",
        default=[],
        help="Chip name (if given, overrides use of chip index). Can be given multiple times.",
    )
    parser.add_argument(
        "-s",
        "--shared-vmux",
        type=int,
        default=None,
        help="Assume FE's have shared vmux, and set MonitorV register to this value (high-Z) on all FE's when not reading",
    )
    parser.add_argument(
        "--read-adc-bin",
        default=None,
        help="Path to the read-adc executable (default: bin/read-adc next to this script's YARR checkout, falling back to $PATH)",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Enable debug print out, including the read-adc commands run and all measured voltages/currents",
    )
    args = parser.parse_args()

    if args.debug:
        log.setLevel(logging.DEBUG)

    read_adc_bin = resolve_read_adc_bin(args.read_adc_bin)
    connectivity_file = Path(args.connectivity_file)

    chips = get_chip_list(connectivity_file)
    if args.chip_name:
        chips = [chip for chip in chips if chip["name"] in args.chip_name]
    elif args.chip_index:
        chips = [chip for chip in chips if chip["index"] in args.chip_index]

    if not chips:
        sys.exit("ERROR: No matching chips found in connectivity file")

    common_kwargs = {
        "chip_indices": args.chip_index,
        "chip_names": args.chip_name,
        "shared_vmux": args.shared_vmux,
    }

    vntc_values = run_read_adc(
        read_adc_bin, args.hw_controller_file, args.connectivity_file, V_MUX_NTC,
        measure_current=False, **common_kwargs,
    )
    vgnd_values = run_read_adc(
        read_adc_bin, args.hw_controller_file, args.connectivity_file, V_MUX_GND,
        measure_current=False, **common_kwargs,
    )
    intc_values = run_read_adc(
        read_adc_bin, args.hw_controller_file, args.connectivity_file, I_MUX_NTC,
        measure_current=True, **common_kwargs,
    )
    ignd_values = run_read_adc(
        read_adc_bin, args.hw_controller_file, args.connectivity_file, I_MUX_GND,
        measure_current=True, **common_kwargs,
    )

    n = len(chips)
    for values, label in (
        (vntc_values, "Vmux2"), (vgnd_values, "Vmux30"),
        (intc_values, "Imux9"), (ignd_values, "Imux63"),
    ):
        if len(values) != n:
            sys.exit(
                f"ERROR: read-adc returned {len(values)} value(s) for {label} but expected {n} chip(s)"
            )

    for chip, vntc, vgnd, intc_ua, ignd_ua in zip(
        chips, vntc_values, vgnd_values, intc_values, ignd_values
    ):
        vntc_net = vntc - vgnd
        intc_net = (intc_ua - ignd_ua) * 1e-6  # uA -> A
        temp = calc_temp_ntc(vntc_net, intc_net, chip["ntc_cal_par"])
        log.debug(
            "Chip %s (index %d): Vmux2=%.6f V, Vmux30=%.6f V, Vntc=%.6f V, "
            "Imux9=%.6f uA, Imux63=%.6f uA, Intc=%.6f uA, Rntc=%.4f ohm, NtcCalPar=%s -> T=%.4f C",
            chip["name"], chip["index"], vntc, vgnd, vntc_net,
            intc_ua, ignd_ua, intc_net * 1e6, vntc_net / intc_net, chip["ntc_cal_par"], temp,
        )
        print(f"{temp:.4f}")


if __name__ == "__main__":
    main()
