#!/bin/bash

#/dev/pmem12 on /mnt/mem0 type ext4 (rw,relatime,seclabel,dax,data=ordered)

set -euo pipefail

. check-nvdimm.sh

test -d results || mkdir results

typeset -A avxname
avxname[0]="sse2"
avxname[1]="avx512f"
typeset -A nodecpus
nodecpus[0]=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
nodecpus[1]=16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
#nodecpus[0]=32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
#nodecpus[1]=48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
# name of directory (off /mnt)
memdir[0]=mem0 
memdir[1]=mem1 
# character with which to fill the memory
fillchar[0]='A' 
fillchar[1]='B' 

for mem in 0 1
do	check_nvdimm ${memdir[$mem]} || exit 1
done

sanity=--sanity-checks
sanity=""
ts=$(date -Iminutes)

mbs=16
mbs=1024
mbs=16381

mbs=8192

for mb in ${mbs}
do	for cpu_node in 0 1
	do	:
		fill=${fillchar[$cpu_node]}
		for dram_node in # 0 1
		do	:
			fn_generic=bw-ncpu${cpu_node}-ndram${dram_node}-mb${mb}.txt
			fn_specific=${ts}-${fn_generic}
set -x
			LD_LIBRARY_PATH=:../../src/lib/common:/usr/local/lib64 \
				time ./nvbench \
				--clkfreq 2.300 --mb ${mb} --cpu-affinity=${nodecpus[$cpu_node]} --bandwidth \
				--dram --numa-dram-affinity=${dram_node} \
				2>&1 | tee results/${fn_specific}
set +x
			(cd results; ln -f ${fn_specific} ${fn_generic})
		done
		for avx in 1 # 0 1
		do	:
			unset PMEM_AVX512F
			if [ $avx -eq 1 ]
			then	export PMEM_AVX512F=1
			fi
			for mem in 0 1
			do	:
				dir=/mnt/${memdir[$mem]}
				rm -f ${dir}/nvbench*
				fn_generic=bw-${avxname[$avx]}-ncpu${cpu_node}-${memdir[$mem]}-mb${mb}.txt
				fn_specific=${ts}-${fn_generic}
				LD_LIBRARY_PATH=:../../src/lib/common:/usr/local/lib64 \
					time ./nvbench \
					--clkfreq 2.300 --mb ${mb} --cpu-affinity=${nodecpus[$cpu_node]} --bandwidth --fillchar=$fill \
					--nvdimm --nv-dir=${dir} \
					2>&1 | tee results/${fn_specific}
				(cd results; ln -f ${fn_specific} ${fn_generic})
			done
		done
	done
done

