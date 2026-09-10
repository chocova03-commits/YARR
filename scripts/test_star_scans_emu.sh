#!/bin/bash

function runScan ()
{
    bin/scanConsole -r ${CONTROLLER} -c ${CONNECTIVITY} -s ${SCAN_CFG}
    if [ $? -eq 0 ]; then
        echo ${scan} OK
    else
        echo ${scan} "FAILED (return code $?)"
        exit 1
    fi
}

# Run PPA version (tests trim feedback with different mapping)
CONNECTIVITY=configs/connectivity/example_star_setup.json

CONTROLLER=configs/controller/emuCfg_star.json
SCAN_CFG=configs/scans/star/std_tune_trim_at_pedestal.json

runScan

CONNECTIVITY=configs/connectivity/example_star_setup_ppb.json

CONTROLLER=configs/controller/starEmu/emuCfg_star_gen_trim.json
SCAN_CFG=configs/scans/star/std_tune_trim_at_pedestal.json
SCAN_CFG=configs/scans/star/full_scan_trim_at_pedestal.json

runScan

CONTROLLER=configs/controller/starEmu/emuCfg_star_gen_sd.json
SCAN_CFG=configs/scans/star/std_strobeDelayScan.json

runScan

SD_CHANGES=$(diff -u <(bin/printStarSubRegisters -c data/last_scan/*.before) <(bin/printStarSubRegisters -c data/last_scan/*.after) | grep "STR_DEL " | wc -l)

# Expect line for add/remove from each of 10 ASICs
if [ $SD_CHANGES -eq 20 ]; then
    echo "SD config update OK"
else
    echo "SD config update failed"
    exit 1
fi

CONTROLLER=configs/controller/starEmu/emuCfg_star_gen_bvt.json
SCAN_CFG=configs/scans/star/std_vt50scan.json

runScan

CONTROLLER=configs/controller/emuCfg_star_ppb.json
SCAN_CFG=configs/scans/star/std_latencyScan.json

runScan

# Run simple check of checkCom (currently disabled by default)

FRONT_END_IN=configs/defaults/default_star_ppb.json
FRONT_END=configs/defaults/default_star_ppb_patch.json
jq '.RunCheckCom = true' ${FRONT_END_IN} > ${FRONT_END}

# Point connectivity to patched front end
CONNECTIVITY_IN=${CONNECTIVITY}
CONNECTIVITY=configs/connectivity/example_star_setup_ppb_patch.json
jq --arg fe "${FRONT_END}" '.chips[0].config = $fe' ${CONNECTIVITY_IN} > ${CONNECTIVITY}

runScan
