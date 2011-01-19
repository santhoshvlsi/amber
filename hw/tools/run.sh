#!/bin/bash 

#--------------------------------------------------------------#
#                                                              #
#  run.sh                                                      #
#                                                              #
#  This file is part of the Amber project                      #
#  http://www.opencores.org/project,amber                      #
#                                                              #
#  Description                                                 #
#  Run a Verilog simulation using Modelsim                     #
#                                                              #
#  Author(s):                                                  #
#      - Conor Santifort, csantifort.amber@gmail.com           #
#                                                              #
#//////////////////////////////////////////////////////////////#
#                                                              #
# Copyright (C) 2010 Authors and OPENCORES.ORG                 #
#                                                              #
# This source file may be used and distributed without         #
# restriction provided that this copyright statement is not    #
# removed from the file and that any derivative work contains  #
# the original copyright notice and the associated disclaimer. #
#                                                              #
# This source file is free software; you can redistribute it   #
# and/or modify it under the terms of the GNU Lesser General   #
# Public License as published by the Free Software Foundation; #
# either version 2.1 of the License, or (at your option) any   #
# later version.                                               #
#                                                              #
# This source is distributed in the hope that it will be       #
# useful, but WITHOUT ANY WARRANTY; without even the implied   #
# warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR      #
# PURPOSE.  See the GNU Lesser General Public License for more #
# details.                                                     #
#                                                              #
# You should have received a copy of the GNU Lesser General    #
# Public License along with this source; if not, download it   #
# from http://www.opencores.org/lgpl.shtml                     #
#                                                              #
#--------------------------------------------------------------#

#--------------------------------------------------------
# Defaults
#--------------------------------------------------------
AMBER_LOAD_MAIN_MEM=" "
AMBER_SIM_CTRL=1
SET_G=0
SET_M=0
SET_D=0
SET_T=0
SET_S=0
SET_V=0
SET_A=0


# show program usage
show_usage() {
    echo "Usage:"
    echo "run <test_name> [-a] [-g] [-d] [-t] [-s] [-v]"
    echo " -h : Help"
    echo " -a : Run hardware tests (all tests in \$AMBER_BASE/hw/tests)"
    echo " -g : Use Modelsim GUI"
    echo " -d <cycle number to start dumping>: Create vcd file"
    echo " -t <cycle number to start dumping>: Create vcd file and terminate"
    echo " -s : Use Xilinx Spatran6 Libraries (slower sim)"
    echo " -v : Use Xilinx Virtex6 Libraries (slower sim)"
    echo ""
    exit
}

#--------------------------------------------------------
# Parse command-line options
#--------------------------------------------------------

# Minimum number of arguments needed by this program
MINARGS=1

# show usage if '-h' or  '--help' is the first argument or no argument is given
case $1 in
	""|"-h"|"--help") show_usage ;;
esac

# get the number of command-line arguments given
ARGC=$#

# check to make sure enough arguments were given or exit
if [[ $ARGC -lt $MINARGS ]] ; then
 echo "Too few arguments given (Minimum:$MINARGS)"
 echo
 show_usage
fi

# self-sorting argument types LongEquals, ShortSingle, ShortSplit, and ShortMulti
# process command-line arguments
while [ "$1" ]
do
    case $1 in
        -*)  true ;
            case $1 in
                -a)     SET_A=1   # all tests
                        shift ;;
                -s)     SET_S=1   # Xilinx libs
                        shift ;;
                -v)     SET_V=1   # Xilinx libs
                        shift ;;
                -g)     SET_G=1   # Bring up GUI
                        shift ;;
                -d)     SET_D=1
                        DUMP_START=$2
                        shift 2;;
                        
                -t)     SET_D=1
                        SET_T=1
                        DUMP_START=$2
                        shift 2;;
                        
                -*)
                        echo "Unrecognized argument $1"
                        shift ;;
            esac ;;  
        * ) AMBER_TEST_NAME=$1
            shift ;;
        
    esac
done


#--------------------------------------------------------
# Set comfiguration based on command-line options
#--------------------------------------------------------

if [ $SET_G == 1 ]; then
    RUN_OPTIONS="-do cmd.do"
else    
    RUN_OPTIONS="${RUN_OPTIONS} -c -do run.do"
fi

if [ $SET_S == 1 ]; then
    FPGA="+define+XILINX_SPARTAN6_FPGA +define+XILINX_FPGA"
    RUN_OPTIONS="${RUN_OPTIONS} -t ps  +notimingchecks -L unisims_ver -L secureip"
else    
    if [ $SET_V == 1 ]; then
        FPGA="+define+XILINX_VIRTEX6_FPGA +define+XILINX_FPGA"
        RUN_OPTIONS="${RUN_OPTIONS} -t ps  +notimingchecks"
    else    
        FPGA=" "
    fi
fi


if [ $SET_D == 1 ]; then
    AMBER_DUMP_VCD="+define+AMBER_DUMP_VCD +define+AMBER_DUMP_START=$DUMP_START"
else    
    AMBER_DUMP_VCD=" "
fi

if [ $SET_T == 1 ]; then
    AMBER_TERMINATE="+define+AMBER_TERMINATE"
else    
    AMBER_TERMINATE=" "
fi

if [ $SET_A == 1 ]; then
    if [ $SET_S == 1 ]; then
        ../tools/all.sh -s
        exit
    elif [ $SET_V == 1 ]; then
        ../tools/all.sh -v
        exit 
    else       
        ../tools/all.sh
        exit
    fi        
fi

#--------------------------------------------------------
# Compile the test
#--------------------------------------------------------

# First check if its an assembly test
if [ -f ../tests/${AMBER_TEST_NAME}.S ]; then
    TEST_TYPE=0
elif [ -d ../../sw/${AMBER_TEST_NAME} ]; then
    # Does this test type need the boot-loader ?
    if [ -e ../../sw/${AMBER_TEST_NAME}/sections.lds ]; then
        grep 8000 ../../sw/${AMBER_TEST_NAME}/sections.lds > /dev/null
        if [ $? == 0 ]; then
            TEST_TYPE=2
        else
            TEST_TYPE=1
        fi
    else
        TEST_TYPE=1
    fi    
else    
    echo "Test ${AMBER_TEST_NAME} not found"
    exit
fi


# Now compile the test
if [ $TEST_TYPE == 1 ]; then
    # sw Stand-alone C test
    echo do ${AMBER_TEST_NAME}
    pushd ../../sw/${AMBER_TEST_NAME} > /dev/null
    make
    MAKE_STATUS=$?
    popd > /dev/null
    BOOT_MEM_FILE="../../sw/${AMBER_TEST_NAME}/${AMBER_TEST_NAME}.mem"
    BOOT_MEM_PARAMS_FILE="../../sw/${AMBER_TEST_NAME}/${AMBER_TEST_NAME}_memparams.v"
    AMBER_LOG_FILE="${AMBER_TEST_NAME}.log"
    AMBER_SIM_CTRL=2

elif [ $TEST_TYPE == 2 ]; then
    # sw Boot-Loader C test
    echo do ${AMBER_TEST_NAME}
    pushd ../../sw/boot-loader > /dev/null
    make
    MAKE_STATUS=$?
    popd > /dev/null
    if [ $MAKE_STATUS != 0 ]; then
        echo "Error compiling boot-loader"
        exit 1
    fi
    
    pushd ../../sw/${AMBER_TEST_NAME} > /dev/null
    make
    MAKE_STATUS=$?
    popd > /dev/null
    
    BOOT_MEM_FILE="../../sw/boot-loader/boot-loader.mem"
    BOOT_MEM_PARAMS_FILE="../../sw/boot-loader/boot-loader_memparams.v"
    MAIN_MEM_FILE="../../sw/${AMBER_TEST_NAME}/${AMBER_TEST_NAME}.mem"
    AMBER_LOAD_MAIN_MEM="+define+AMBER_LOAD_MAIN_MEM"
    AMBER_LOG_FILE="${AMBER_TEST_NAME}.log"

else
    # hw assembly test
    echo "Compile ../tests/${AMBER_TEST_NAME}.S"
    pushd ../tests > /dev/null
    make TEST=${AMBER_TEST_NAME}
    MAKE_STATUS=$?
    popd > /dev/null
    BOOT_MEM_FILE="../tests/${AMBER_TEST_NAME}.mem"
    BOOT_MEM_PARAMS_FILE="../tests/${AMBER_TEST_NAME}_memparams.v"
    AMBER_LOG_FILE="hw-tests.log"
fi


#--------------------------------------------------------
# Modelsim
#--------------------------------------------------------
if [ $MAKE_STATUS == 0 ]; then
   if [ ! -d work ]; then
       vlib work
   fi

   if [ $? == 0 ]; then
       vlog +libext+.v \
            +incdir+../vlog/amber+../vlog/system+../vlog/tb+../vlog/ethmac \
            +incdir+../vlog/lib+../vlog/xs6_ddr3+../vlog/xv6_ddr3 \
            -y ../vlog/amber -y ../vlog/system  -y ../vlog/tb -y ../vlog/ethmac \
            -y ../vlog/lib   -y ../vlog/xs6_ddr3 -y ../vlog/xv6_ddr3  \
            -y $XILINX/verilog/src/unisims \
            -y $XILINX/verilog/src \
            ../vlog/tb/tb.v \
            $XILINX/verilog/src/glbl.v \
            +define+BOOT_MEM_FILE=\"$BOOT_MEM_FILE\" \
            +define+BOOT_MEM_PARAMS_FILE=\"$BOOT_MEM_PARAMS_FILE\" \
            +define+MAIN_MEM_FILE=\"$MAIN_MEM_FILE\" \
            +define+AMBER_LOG_FILE=\"$AMBER_LOG_FILE\" \
            +define+AMBER_TEST_NAME=\"$AMBER_TEST_NAME\" \
            +define+AMBER_SIM_CTRL=$AMBER_SIM_CTRL \
            ${FPGA} \
            $AMBER_DUMP_VCD \
            $AMBER_TERMINATE \
            $AMBER_BEH_MEM \
            $AMBER_LOAD_MAIN_MEM
                  
        if [ $? == 0 ]; then
            vsim -voptargs="+acc=rnpc" tb ${RUN_OPTIONS}
        fi
   fi

else
    echo "Failed " $AMBER_TEST_NAME " compile error" >> $AMBER_LOG_FILE
fi



