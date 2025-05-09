#!/bin/bash

## run it from gem5 main path

inCim=1 ## enable this flag if scons is compiled with 'CDNCcim=1' flag
var=1
EndAddress=0x12000018
FileSizes=(1)
optimization="-O3 -Wall -Wconversion -Wno-sign-conversion -DSTD_PRINT_OUTPUT" #-DCHECKPOINT_FI -DSTD_PRINT_OUTPUT
tag=O3stt

bold=$(tput bold)
blink=$() # tput blink
blue=$(tput setaf 6)
green=$(tput setaf 2)
red=$(tput setaf 1)
normal=$(tput sgr0)

echo -e "\t${bold}${blue}Compiling the source code...${normal}"

fout=app1_bit_indexing
# fout=app2_aes_ebc_enc

g++ $optimization \
	./tests/test-progs/CDNCcim/$fout.cpp \
	./tests/test-progs/CDNCcim/cim_api.cpp \
	-o ./tests/test-progs/CDNCcim/$fout.exe \
	-I ./include/ -lm5 -L ./util/m5/build/x86/out \
	-DNUM_ROWS=$var \
	-DNUM_WEEKS=$var \
	-DinCIM=$inCim \
	-DEndAddress=$EndAddress

echo -e "\t${bold}${blue}Running Simulation...${normal}"

build/X86/gem5.opt --stdout-file=out.txt --stderr-file=err.txt \
	--stats-file=stat.txt --debug-file=debug.txt \
	configs/CDNCcim/system_design_$tag.py "./tests/test-progs/CDNCcim/$fout.exe" \
	--EndAddress $EndAddress

echo -e "\n\t${bold}${blue}$fout ${blink}is Finished.${normal}"

rm "./tests/test-progs/CDNCcim/$fout.exe"

echo -e "---------------------------------------"
