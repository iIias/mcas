#!/bin/bash
if [ "$1" == "part2" ]
then
    sudo ndctl destroy-namespace all -f
    REGIONS=`ndctl list -R | egrep -oh 'region[0-9]+' | egrep -oh '[0-9]+'`
    echo "Creating namespaces... this will take a while!"
    # region 0
	  for p in {0..19}
	  do
	      sudo ndctl create-namespace -m devdax --align 2M -r region1 -s 32G
	  done
    # region 1
    sudo ndctl create-namespace -m devdax --align 2M -r region1 -s 2688G
    
    sudo chmod a+rwx /dev/dax*
    ls -l /dev/dax*
else
    read -p "Are you sure you want to erase AEP namespaces?" -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]
    then
        echo "Creating goals..."
        sudo ndctl destroy-namespace all --force
        sudo ipmctl delete -goal
        sudo ipmctl create -goal PersistentMemoryType=AppDirect
        echo "Goal configuration complete. Reboot and then run this script with first argument \"part2\""
        exit
    fi
fi

