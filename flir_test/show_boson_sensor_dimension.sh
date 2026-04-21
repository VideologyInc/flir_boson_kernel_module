#!/bin/bash

echo "Show Boson sensor dimension from ClockInfo"

dmesg | grep -A 2 -i "ClockInfo"

