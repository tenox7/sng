# SNG TODO

## Bugs

- SNMP on FreeBSD

## Features

- time scale, recognize gaps or misalignment of data in graphs
- logarithmic scale as well
- vertical bar with latest value
- vertical scale fine controls, auto, margin, max, hardmax
- rethink scale for ping, hard to know what it is it, maybe make it ot 10,100,200,300 etc?
- horizontal guides ie softmax or ncpu for load
- logarithmic ringbuf
- save ringbuf to file
- makefiles per platform eg Makefile.aix?

## Data Sources

- implement more stats, look at: top, xosview, net-snmp, perf meter
- modern stats like temperature, wifi signal/rssi, battery level
- interface errors, drops, etc local and snmp
- storage: IOPS, MB/s from iostat
- snmp v2 getifx
- IPv6 support

## Platforms

- OpenServer/OpenDesktop
- NextStep/OpenStep
- OpenVMS
- Win32/NT
- Plan9
- RiscOS
- QNX6 / QNX4


