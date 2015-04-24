#!/bin/bash 

val=${*:1}
pintos --gdb --dport=5444 -v -T 60 -k  --qemu --filesys-size=2 -p build/tests/userprog/$1 -a $1 -- -f -q run "$val" < /dev/null
