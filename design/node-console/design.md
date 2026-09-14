# Node console design

Status: implemented; runtime acceptance scope is recorded in [verification](verification.md).

All authored graphs use the same console facility. An owned C++ log relay reads only
resources in the existing graph ledger, validates runtime identities, and publishes
bounded snapshots into a dedicated console directory. The platform receives only
that directory; Docker, QMP and the ownership ledger are not mounted into it.
This file handoff also works across macOS/OrbStack filesystem sharing without
forwarding a privileged socket. The relay is an owned child of the existing lifecycle.

Native and namespace stdout/stderr are already merged by the process launcher.
Container output is obtained through bounded Docker log queries against the exact
owned container. QEMU boot diagnostics remain on ttyS0's existing bounded ring.
A dedicated second serial port (ttyS1) provides interactive access through a Unix
socket in the console directory. Only that serial endpoint reaches the platform.
The platform owns the serial reader and fans output out to browser viewers.

Guest login is supplied by explicitly selected guest images. No guest account,
password, shell or login service is provisioned by this feature. A guest image must
configure its interactive service on ttyS1. The maintained images do not promise
an interactive login. QMP management and boot readiness remain separate.

The web Console panel has Logs and Serial views. Log snapshots are observation
protected; serial input additionally requires an explicit node-scoped `serial`
control grant. Existing console handoff sessions and bearer authentication apply.
One writer lease per node is bound to its principal and runtime generation, expires
on inactivity, and is released on disconnect. Every input request checks current
credentials, origin, CSRF and generation. Guest output is untrusted text.

Limits: 64 KiB retained output per source, bounded polling rather than persistent
HTTP responses, 4 KiB maximum serial input, bounded leases and requests, and explicit
gaps whenever a retained snapshot cannot continue the viewer's previous cursor.
Logs preserve the source's merged-stream semantics. Stopping log following or
releasing a writer does not stop a workload. Identity mismatch refuses access.

Implementation sequence: existing-path characterization; owned log handoff; platform
API and panel; dedicated serial endpoint and writer authorization; focused negative
checks; quick/quality/portable verification; separately authorized Linux/Lima guest
and browser acceptance. Privileged acceptance is not authorized by this design.

## Integration and recovery details

The existing QEMU diagnostic scenarios consume the boot ring as well. Relay reads
hold the common shared lock while scenario actions hold its exclusive lock; both
mirror consumed bytes to a 64 KiB boot window. This preserves scenario evidence
and prevents the observer from consuming a VLAN-isolation probe behind the test.
The separate interactive serial stream has one platform reader.

The console directory and serial socket device/inode identities are recorded in
the existing ownership ledger. The platform validates a socket against the relay's
identity-checked snapshot before connecting and during session sweeps. QEMU inherits
only a descriptor to the console directory, avoiding broader traversal permissions
on the private graph state root. Its dedicated host UID remains 65532.

Native/namespace output retains the launcher's merged-stream semantics. Container
queries validate ID, owner, graph and image before requesting bounded output. The
relay takes no graph mutation lock for these reads. Teardown stops it through the
common process lifecycle. Transient OS identity unavailability after signaling is
retried without additional signals; escalation still requires exact identity.

The platform's socket reader retains 64 KiB, up to 64 guest connections, with a
30-second idle viewer timeout. Writer leases expire after ten seconds without
renewal and have a fifteen-minute maximum bounded by browser-session expiry.
Requests carry at most 4 KiB input, with bounded queues and five-second response
inactivity deadlines. Polling runs every 1.5 seconds. Log windows and serial byte
cursors report possible or known output gaps. No complete durable log archive is
promised. The next owned startup replaces the previous console window.

The terminal uses pinned MIT-licensed `@xterm/xterm` 5.5.0, with no link activation,
clipboard or title side effects from guest output. Its [upstream API](https://xtermjs.org/docs/api/terminal/classes/terminal/)
defines byte-stream rendering and input events. The existing platform bundle copies
runtime modules and builds locked web dependencies, so no separate service image or
runtime package loader was added.
