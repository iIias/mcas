#!/bin/bash

set -euo pipefail

. check-nvdimm.sh

typeset -A avxname
avxname[0]="sse2"
avxname[1]="avx512f"
flushname[0]="clwb"
flushname[1]="clflushopt"
memdir[0]=mem0 
memdir[1]=mem1 

ts=$(date -Iminutes)
hs=500
hi=10

test -d results || mkdir results
sanity=--sanity-checks
sanity=""

for mem in 0 1
do	check_nvdimm ${memdir[$mem]} || exit 1
done

mbs="16"
mbs="1024"
mbs="8192"

for mb in $mbs
do	for cpu_id in 0
	do	:
		for dram_node in 0 1
		do	:
			fn_generic=la-ncpu${cpu_id}-ndram${dram_node}-mb${mb}.txt
			fn_specific=${ts}-${fn_generic}
set -x
			LD_LIBRARY_PATH=:../../src/lib/common:/usr/local/lib64 \
				time ./nvbench \
				--clkfreq 2.100 --mb ${mb} --cpu-affinity=${cpu_id} --histogram-size=${hs} --histogram-incr=${hi} \
				${sanity} \
				--dram --numa-dram-affinity=${dram_node} \
				2>&1 | tee results/${fn_specific}
set +x
			(cd results; ln -f ${fn_specific} ${fn_generic})
		done
		for avx in # 1 # 0
		do	:
			for flushopt in 1 # 0 1
			do	:
				unset PMEM_AVX512F
				if [ $avx -eq 1 ]
				then	: # Enable memset/memcpy use if 512-bit vector registers
					export PMEM_AVX512F=1
				fi
				unset PMEM_NO_CLWB
				if [ $flushopt -eq 1 ]
				then	: # Tell pmemobj not to use clwb, which cause it to use clflushopt, of available, for flushes
					export PMEM_NO_CLWB=1
				fi
				for mem in 0 1
				do	:
					dir=/mnt/${memdir[$mem]}
					rm -f ${dir}/nvbench*
					fn_generic=la-${avxname[$avx]}-${flushname[$flushopt]}-ncpu${cpu_id}-${mem}-mb${mb}.txt
					fn_specific=${ts}-${fn_generic}
set -x
					LD_LIBRARY_PATH=:../../src/lib/common:/usr/local/lib64 \
						time ./nvbench \
						--clkfreq 2.100 --mb ${mb} --cpu-affinity=${cpu_id} --histogram-size=${hs} --histogram-incr=${hi} \
						${sanity} \
						--nvdimm --nv-dir=${dir} \
						2>&1 | tee results/${fn_specific}
set +x
					(cd results; ln -f ${fn_specific} ${fn_generic})
				done
			done
		done
	done
done

