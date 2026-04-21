#!/bin/bash

echo "Show Boson sensor dimension from ClockInfo"

journalctl -b | grep -A 2 -i "ClockInfo"

