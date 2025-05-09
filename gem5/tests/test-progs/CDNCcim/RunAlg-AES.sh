#!/bin/bash

inCim=0
EndAddress=0x12000000
FileRows=(1)
tag=test
optimization="-O3 -Wall -Wconversion -Wno-sign-conversion"

bold=$(tput bold)
blink=$() # tput blink
blue=$(tput setaf 6)
green=$(tput setaf 2)
red=$(tput setaf 1)
normal=$(tput sgr0)

usage="
$(basename "$0") [-c] [-e EndAddress ] [-f "FileRows..."] [-o "g++Flags..."] -t TagName

${bold}${green}AES-EBC encryption algorithm Run Script:${normal}
	[-c]	in CIM computation
	[-e]	EndAddress in hex format 	default:	0x15200010
	[-f]	File Sizes in string format	default:	\"1\"
	[-o  \"g++ compile flags\"]		default:	\"-O3 -Wall -Wconversion -Wno-sign-conversion\"
	 -t   TagName
"

options=':hce:f:o:t:'
while getopts $options option; do
	case "$option" in
	h)
		echo -e "$usage"
		exit
		;;
	c) inCim=1 ;;
	e) EndAddress=$OPTARG ;;
	f) FileRows=$OPTARG ;;
	o) optimization=$OPTARG ;;
	t) tag=$OPTARG ;;
	:)
		printf "${bold}${red}missing argument for ${green} -%s ${normal}\n" "$OPTARG" >&2
		echo "$usage" >&2
		exit 1
		;;
	\?)
		printf "${bold}${red}illegal option: ${green} -%s ${normal}\n" "$OPTARG" >&2
		echo "$usage" >&2
		exit 1
		;;
	esac
done

echo -e -n "${bold}${blue}AES...${normal}
	inCim:		$inCim
	EndAddress:	$EndAddress
	g++ flags:	$optimization
${bold}	Tag:		$tag ${normal}
	FileRows:\t"
for var in ${FileRows[@]}; do
	echo -e -n "$var,  "
done

echo -e "\n\n"

for var in ${FileRows[@]}; do

	echo -e "\t${bold}${blue}Compiling the source code...${normal}"

	cd /

	if [ $inCim -eq 0 ]; then
		fout=AES-CPU\_$var\_$tag
	else
		fout=AES-CIM\_$var\_$tag
	fi

	g++ $optimization \
		/nv-gem5/tests/test-progs/CDNCcim/app2_aes_ebc_enc.cpp \
		/nv-gem5/tests/test-progs/CDNCcim/cim_api.cpp \
		-o /nv-gem5/OUTPUTS/$fout.exe \
		-I gem5/include/ -lm5 -Lgem5/util/m5/build/x86/out \
		-DNUM_ROWS=$var \
		-DinCIM=$inCim

	cd /nv-gem5/

	echo -e "\t${bold}${blue}Running Simulation...${normal}"

	build/X86/gem5.fast --outdir=OUTPUTS/aliOUTPUTS/$fout -e -r --stdout-file=out-$fout.txt \
		--stderr-file=err-$fout.txt --stats-file=stat-$fout.txt \
		configs/CDNCcim/system_design_$tag.py "/nv-gem5/OUTPUTS/$fout.exe" \
		--EndAddress $EndAddress

	echo -e "\n\t${bold}${blue}$fout ${blink}is Finished.${normal}"

	tput setaf 2
	tail -n 3 OUTPUTS/aliOUTPUTS/$fout/out-$fout.txt
	tput sgr0

	tail -n 3 OUTPUTS/aliOUTPUTS/$fout/out-$fout.txt >>OUTPUTS/aliOUTPUTS/TEST_Result_$tag.txt

	echo -e "---------------------------------------"
done
