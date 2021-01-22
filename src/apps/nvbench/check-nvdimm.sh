#!/bin/bash

function check_nvdimm
{
	typeset mem="${1}"
	shift
	if ! ( mount | grep "on /mnt/$mem .*[(,]dax[,)]" )
	then	echo "Mount point /mnt/$mem not found (or not dax)"
		echo "You may need to:"
		echo " sudo mkdir -m 777 /mnt/$mem"
		echo " sudo mkfs.ext4 /dev/pmemNN OR #sudo mkfs.xfs /dev/pmemNN"
		echo " sudo mount -o dax /dev/pmemNN /mnt/$mem"
		false
	fi
}
