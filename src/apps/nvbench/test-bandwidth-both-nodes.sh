#!/bin/bash

#/dev/pmem12 on /mnt/mem0 type ext4 (rw,relatime,seclabel,dax,data=ordered)

set -euo pipefail

. check-nvdimm.sh

test -d results || mkdir results

typeset -A avxname
avxname[0]="sse2"
avxname[1]="avx512f"
typeset -A nodecpus

# Run with hyperthreads?
if false
then	nodecpus[0]=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	nodecpus[1]=16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	ht=""
else
	nodecpus[0]=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
	nodecpus[1]=16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
	ht="ht-"
fi

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

# Tell pmemobj not to use clwb, which cause it to use clflushopt, of available, for flushes
export PMEM_NO_CLWB=1

# Maximum pmem object size is 16381MiB (16384MiB less some overhead).
mbs="64 512 4096 16381"
mbs="16"
mbs="4096"
mbs="8192"

for mb in ${mbs}
do	:
	if true # false
	then	:
		fn_generic=bw-both-${ht}dram-mb${mb}.txt
		fn_specific=${ts}-${fn_generic}
set -x
		LD_LIBRARY_PATH=:../../src/lib/common:/usr/local/lib64 \
			time ./nvbench \
			${sanity} \
			--clkfreq 2.100 --mb ${mb} --dram --bandwidth \
			--cpu-affinity=${nodecpus[0]} --numa-dram-affinity=0  \
			--cpu-affinity=${nodecpus[1]} --numa-dram-affinity=1  \
			--size 64 --size 512 --size 4096 \
			2>&1 | tee results/${fn_specific}
set +x
		(cd results; ln -f ${fn_specific} ${fn_generic})
	fi
	for avx in 1 # 0 1
	do	:
		unset PMEM_AVX512F
		if [ $avx -eq 1 ]
		then	export PMEM_AVX512F=1
		fi
		for mem in 0 1
		do	dir=/mnt/${memdir[$mem]}
			rm -f ${dir}/nvbench*
		done
		fn_generic=bw-${ht}${avxname[$avx]}-both-mb${mb}.txt
		fn_specific=${ts}-${fn_generic}
set -x
		LD_LIBRARY_PATH=:../../src/lib/common:/usr/local/lib64 time ./nvbench \
				--clkfreq 2.100 --mb ${mb} --nvdimm --bandwidth fillchar=X \
				${sanity} \
				--cpu-affinity=${nodecpus[0]} --nv-dir=/mnt/${memdir[0]} \
				--size 64 --size 512 --size 4096 \
				--cpu-affinity=${nodecpus[1]} --nv-dir=/mnt/${memdir[1]} \
				2>&1 | tee results/${fn_specific}
set +x
		(cd results; ln -f ${fn_specific} ${fn_generic})
	done
done

