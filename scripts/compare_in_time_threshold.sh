#!/usr/bin/env bash
#
# compare_in_time_threshold.sh
#
# Usage:
#   ./scripts/compare_in_time_threshold.sh -r <CONTROLLER> -c <CONNECTIVITY>
#
# Example:
#   ./scripts/compare_in_time_threshold.sh -r configs/controller/specCfg-rd53b-16x1.json \
#     -c module-qc-database-tools/20UPIM00000000/20UPIM00000000_L1_warm.json
#
# This script checks the chipType in the connectivity file.
#   - If "chipType" is "ITKPIXV2", it uses scans/itkpixv2
#   - If "chipType" is "RD53B", it uses scans/rd53b
#   - Otherwise, it exits

set -e
set -o pipefail

show_help() {
  echo "Usage: $0 -r <CONTROLLER> -c <CONNECTIVITY>"
  echo ""
  echo "Example:"
  echo "  $0 -r configs/controller/specCfg-rd53b-16x1.json -c module-qc-database-tools/20UPIM00000000/20UPIM00000000_L1_warm.json"
  exit 1
}

# Parse command-line arguments
while getopts "r:c:" opt; do
  case ${opt} in
    r ) CONT_CONFIG="$OPTARG" ;;
    c ) CONN_CONFIG="$OPTARG" ;;
    * ) show_help ;;
  esac
done

#Check if connectivity and controller files are provided
if [[ -z "${CONT_CONFIG}" || -z "${CONN_CONFIG}" ]]; then
  show_help
fi

# Read chipType from the JSON config
chip_type=$(
  grep -i '"chipType"' "${CONN_CONFIG}" 2>/dev/null \
    | sed -nE 's/.*"chipType"[[:space:]]*:[[:space:]]*"([^"]*)".*/\1/ip' \
    | tr '[:upper:]' '[:lower:]'
)

if [[ -z "$chip_type" ]]; then
  echo "Warning: unable to detect a valid \"chipType\" in ${CONN_CONFIG}."
  echo "Exiting"
  exit 1
fi

# Determine scan directory based on chipType
if [[ "${chip_type}" == "itkpixv2" ]]; then
  SCAN_DIR="itkpixv2"
elif [[ "${chip_type}" == "rd53b" ]]; then
  SCAN_DIR="rd53b"
else
  echo "Warning: unknown chipType \"${chip_type}\" in ${CONN_CONFIG}."
  echo "Exiting"
  exit 1
fi

echo "Detected chipType: ${chip_type}"
echo "Using scan directory: ${SCAN_DIR}"

# Run scans
./bin/scanConsole -r "${CONT_CONFIG}" -c "${CONN_CONFIG}" -s "configs/scans/${SCAN_DIR}/std_thresholdscan.json"
./bin/scanConsole -r "${CONT_CONFIG}" -c "${CONN_CONFIG}" -s "configs/scans/${SCAN_DIR}/std_tune_injdelay.json"
./bin/scanConsole -r "${CONT_CONFIG}" -c "${CONN_CONFIG}" -s "configs/scans/${SCAN_DIR}/std_thresholdscan_intime.json"

echo "All scans completed successfully."
