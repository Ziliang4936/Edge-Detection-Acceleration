#!/usr/bin/env bash
# Probe the Kria environment for CiFra OpenBus driver source / metadata.
set -u
echo '=== lsmod ==='
lsmod | grep -i cifra
echo '=== dmesg (cifra) ==='
dmesg | grep -iE 'cifra|openbus|hls[-_]bridge' | tail -n 20
echo '=== find driver source ==='
for root in /home/ubuntu /root /opt /etc /lib/modules; do
    find "${root}" -maxdepth 5 -iname '*cifra*' -print 2>/dev/null
    find "${root}" -maxdepth 5 -iname '*openbus*' -print 2>/dev/null
done | head -n 60
echo '=== devtree ==='
ls /proc/device-tree 2>/dev/null | head -n 40
echo '=== /dev nodes ==='
ls -la /dev/cifra_openbus_* 2>/dev/null
echo '=== device capabilities probe ==='
cat /proc/devices | grep -iE 'cifra|openbus|your'
echo '=== end ==='
