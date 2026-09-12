# Declarative network observation and faults

This example adds an OVS mirror capture and a time-bounded netem fault to the owned
network lifecycle. Preview with `graphx infra create graphx.yaml --dry-run`, then use
create, status, and destroy on a privileged Linux runtime.

Application captures contain GraphX envelopes as LINKTYPE_USER0. OVS mirror captures
contain Ethernet PCAPNG; the two evidence sources remain separate trust domains.
