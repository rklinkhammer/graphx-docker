#!/usr/bin/env python3
"""Validate resolved resources and the explicit P1 realization gate."""
import sys
from config_plan_support import check

for example in ['qemu-node/tap']:
    check(sys.argv[1], sys.argv[2], example)
print("resolved resources and execution gate passed")
