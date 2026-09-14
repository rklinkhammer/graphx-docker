#!/usr/bin/env python3
"""Validate resolved resources and the explicit P1 realization gate."""
import sys
from config_plan_support import check

for example in ['macvlan', 'ipvlan-l2', 'ipvlan-l3', 'sample-pipeline/ovs']:
    value = check(sys.argv[1], sys.argv[2], example)
    if example == 'sample-pipeline/ovs':
        assert all(node['parameters']['max_messages'] == 0 for node in value['nodes'])
        assert len(value['network']['attachments']) == 3
        assert all(a['kind'] == 'container_veth' and a['switch'] == 'pipeline-switch'
                   for a in value['network']['attachments'])
        assert value['network']['edge_paths'] == {
            'samples': ['generator', 'pipeline-data', 'pipeline-switch', 'transform'],
            'transformed': ['transform', 'pipeline-data', 'pipeline-switch', 'sink'],
        }
print("resolved resources and execution gate passed")
