#!/bin/bash

NODE=0

if [[ "x$1" != "x" ]]; then
	NODE=$1
fi

echo "Cleanup dmesg..."
sudo dmesg -c >/dev/null

echo "Insert kernel module to scan node ${NODE}"
sudo insmod pref-test.ko nid_to_scan=${NODE}

dmesg > scan_range_node_${NODE}_results

echo "Remove kernel module"
sudo rmmod pref_test

cat scan_range_node_${NODE}_results | sed "s/\[.\+\]//" | awk '{start = strtonum("0x" substr($1, 2, length($1)-2)); end = strtonum("0x" substr($2, 1, length($2)-2)); print((end-start)*4/1024 "MB: " $3) }' > scan_size_node_${NODE}_results

echo "The results are in scan_range_node_${NODE}_results and scan_size_node_${NODE}_results"
