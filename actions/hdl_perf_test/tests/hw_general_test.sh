#!/bin/bash
    
##
## Copyright 2019 International Business Machines
##
## Licensed under the Apache License, Version 2.0 (the "License");
## you may not use this file except in compliance with the License.
## You may obtain a copy of the License at
##
##     http://www.apache.org/licenses/LICENSE-2.0
##
## Unless required by applicable law or agreed to in writing, software
## distributed under the License is distributed on an "AS IS" BASIS,
## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
## See the License for the specific language governing permissions and
## limitations under the License.
##

verbose=0
ocaccel_card=0
short=1

# Get path of this script
THIS_DIR=$(dirname $(readlink -f "$BASH_SOURCE"))
ACTION_ROOT=$(dirname ${THIS_DIR})
OCACCEL_ROOT=$(dirname $(dirname ${ACTION_ROOT}))

echo "Starting :    $0"
echo "OCACCEL_ROOT :   ${OCACCEL_ROOT}"
echo "ACTION_ROOT : ${ACTION_ROOT}"

function usage() {
    echo "Usage:"
    echo "  test_<action_type>.sh"
    echo "  test function bridge with hdl_perf_test with different axi_size and different number of axi_id"
    echo "    [-C <card>] card to be used for the test"
    echo "    [-s] choose to run long or short test:"
    echo "         if this argument is not given or given as 1, run shorter test for simulation"
    echo "         if this argument is given as 0, run longer test on card"
    echo
}

while getopts ":C:s:h" opt; do
    case $opt in
        C)  
        ocaccel_card=$OPTARG;
        ;;
        s)  
        short=$OPTARG;
        ;;
        h)
        usage;
        exit 0;
        ;;
        \?)
        echo "Invalid option: -$OPTARG" >&2
        ;;
    esac
done

export PATH=$PATH:${OCACCEL_ROOT}/software/tools:${ACTION_ROOT}/sw

#### VERSION ##########################################################

ocaccel_maint -C ${ocaccel_card} || exit 1;

#### Run CMD ##########################################################

function test_perf_test {
    echo "---------------- Testing burst num $1 size $2 length $3 id num $4: --------------"
    local num=$1
    local size=$2
    local length=$3
    local idn=$4

    local pattern=$(($(($idn<<16))+$(($length<<8))+$size))

    echo -n "Read and Write Duplex ... "
    cmd="hdl_perf_test -C${ocaccel_card} -c 1 -w 0 -n ${num} -N ${num} -p ${pattern} -P ${pattern} -t 100000 >>  hdl_perf_test_general.log 2>&1"
    eval ${cmd}
    if [ $? -ne 0 ]; then
        echo "cmd: ${cmd}"
        echo "failed, please check hdl_perf_test_general.log"
        exit 1
    fi
    echo "ok"

}

rm -f hdl_perf_test_general.log
touch hdl_perf_test_general.log

############## Test small burst length  ##############
test_perf_test 10 7 0 0
test_perf_test 10 7 1 0

############## Test axi_size(2-7) and axi_id_num(0-15) ##############
id=0
while [ $id -lt 16 ]
do
    if [ $short -ne 1 ]; then
        echo "Run long test, preferred to run on card"
        test_perf_test 10000 2 255 ${id}
        test_perf_test 10000 3 255 ${id}
        test_perf_test 10000 4 255 ${id}
        test_perf_test 10000 5 127 ${id}
        test_perf_test 10000 6 63  ${id}
        test_perf_test 10000 7 31  ${id}
    else
        echo "Run short test, preferred to run for simulation"
        test_perf_test 5 2 255 ${id}
        test_perf_test 5 3 255 ${id}
        test_perf_test 5 4 255 ${id}
        test_perf_test 5 5 127 ${id}
        test_perf_test 5 6 63  ${id}
        test_perf_test 5 7 31  ${id}
    fi
    id=$(($id + 1))
done

##Print build date and version
#echo
#echo -n "Git Version: "
##ocaccel_peek -C ${ocaccel_card} 0x0 || exit 1;
#echo -n "Build Date:  "
##ocaccel_peek -C ${ocaccel_card} 0x8 || exit 1;

echo "ok"

rm -f *.bin *.bin *.out
echo "Test OK"
exit 0