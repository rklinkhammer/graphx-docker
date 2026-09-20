# Scoped Lima recovery proposal

Status: proposed, not authorized or executed. This proposal covers only recovery
of the dedicated `graphx` Lima environment. P3/P4 privileged acceptance remains
a separate authorization in the [operator runbook](privileged-verification-runbook.md).

## Observed state and recommendation

Read-only inspection on 2026-09-20 found Lima 2.2.0 and the `graphx` instance
Stopped, using VZ/aarch64, 4 CPUs, 8 GiB RAM and an 80 GiB virtual disk.
Its directory is `/Users/rklinkhammer/.lima/graphx`; allocated storage is about
59 GiB. The Mac has about 63 GiB available. Guest contents and installed packages
have not been inspected; existing guest evidence must be presumed valuable.

Recorded definition fingerprint:
`ac49027a1cbafa52b3565d32a4154fe3ecf3a3aae0ae4a1760782f79da0e5791`.
Current required fingerprint:
`995153b13806694ef031f1b07218f9eafd2c31043deb33457504497cb153339e`.
The fingerprint covers `graphx.yaml` and `provision.sh`. P3 added `ethtool` to
provisioning. The launcher correctly refuses a stale definition; this is not
evidence of disk corruption. Do not edit either fingerprint to bypass the check.

Recommend cold preservation to a user-selected external APFS volume with at least
100 GiB free, followed by replacement through the existing repository launcher.
Do not put the backup inside `.lima/graphx`, the repository, or another directory
that deletion will remove. Local free space is insufficient for a conservative
full-size backup plus replacement headroom. Do not reclaim other data to make room.
An APFS clone or Lima clone on the same disk is not the proposed independent backup.

Do not boot the old instance to inventory it: its provisioning command references
the current writable repository mount and may execute current provisioning against
old guest state. Preserve the stopped disk first.

## Authorized effects if approved

- Read and back up this stopped instance, including disk, EFI state, configuration
  and logs. Exclude only its two stale Unix sockets. Keep the backup private.
- Delete only the registered `graphx` instance after the preservation gate passes.
  This removes its original guest filesystem, including Docker images, volumes,
  `/var/lib/graphx` runtime state, captures and evidence. Those remain in the backup.
- Recreate `graphx` using checked-in `infrastructure/lima/start.sh`. This downloads
  the pinned Ubuntu image, installs guest packages, configures and starts guest
  Docker/OVS, installs Node 24 and pulls provisioning images. Guest package versions
  are recorded by the provisioner. Allow its normal Docker-group refresh restart.
- Run bounded environment readiness probes only. Leave the new VM running and
  report its identity, disk space, package versions and readiness.

Preserve the uncommitted worktree, host preparation artifacts, vrt_framework pin,
OrbStack, other VMs and host Docker context. No graph start, acceptance harness,
image publication, VM-wide verification suite, source reset, push or deployment.
Do not restore old ownership ledgers into the new guest.

## Execution sequence after authorization

Run on the Mac from `/Users/rklinkhammer/workspace/graphx-docker`.
Commands below are a reviewed sequence, not an unattended script: each gate must
pass before the next stage. `BACKUP_PARENT` must identify the approved mounted
external volume, verified with `df`; do not create a missing mountpoint.

1. Record `git status --short`, `limactl list graphx --json`, the current definition
   digest, host free space and external-volume identity/free space. Recheck that
   name, repo path, VZ/aarch64 and recorded fingerprint match the state above and
   status is Stopped. Stop on any change. Inventory file types: unexpected symlinks,
   external disk attachments or active VM processes require revising this proposal.
2. Create an absent, private backup directory and preserve the stopped instance:

   ```sh
   BACKUP_PARENT=/Volumes/APPROVED_VOLUME
   test -d "$BACKUP_PARENT"
   df -h "$BACKUP_PARENT"
   RECOVERY_DIR="$BACKUP_PARENT/graphx-recovery-$(date -u +%Y%m%dT%H%M%SZ)"
   (umask 077; mkdir "$RECOVERY_DIR")
   /usr/bin/rsync -aS --extended-attributes \
     --exclude=/default_ep.sock --exclude=/default_fd.sock \
     /Users/rklinkhammer/.lima/graphx/ "$RECOVERY_DIR/instance/"
   /usr/bin/rsync -aScn --extended-attributes --itemize-changes \
     --exclude=/default_ep.sock --exclude=/default_fd.sock \
     /Users/rklinkhammer/.lima/graphx/ "$RECOVERY_DIR/instance/"
   ```

   Require both commands to exit zero and the checksum comparison to produce no
   differences. Independently SHA-256 every regular source and backup file, compare
   relative file inventories and hashes, and retain that manifest outside `instance`.
   Recheck Stopped and unchanged source metadata after verification. Preserve the
   read-only identity record with the backup. A byte-verified cold copy preserves
   evidence; bootable restoration remains untested. Failure, disk exhaustion or
   interrupted copying means **no deletion**. Never delete the backup automatically.
3. Immediately recheck the identity, backup verification and unchanged definition
   digest. Remove only the stopped original and recreate through the common launcher:

   ```sh
   python3 infrastructure/lima/run-bounded.py 300 limactl delete --tty=false graphx
   bash infrastructure/lima/start.sh
   ```

   Do not use `--force`, raw directory removal, `limactl edit`, a fingerprint patch
   or a direct old-instance start. The launcher bounds create/start to 1800 seconds
   each and has bounded readiness checks. If creation or provisioning fails, retain
   its state and logs and stop; do not repeatedly delete/recreate or restore blindly.
4. Confirm the new instance and guest environment:

   ```sh
   bash -c 'source infrastructure/lima/common.sh; graphx_lima_require_host;
     test "$(graphx_lima_assert_identity "$(graphx_lima_digest)")" = Running'
   python3 infrastructure/lima/run-bounded.py 120 \
     limactl shell --workdir /workspace/graphx-docker graphx -- bash -lc '
       set -euo pipefail
       uname -sm
       docker info >/dev/null
       docker compose version
       docker buildx version
       node --version
       python3 -c "import yaml"
       for tool in ip tc nft dumpcap tshark capinfos ethtool nsenter; do
         command -v "$tool"
       done
       sudo -n ovs-vsctl --timeout=5 show
       cat /etc/graphx-lima-config.sha256
       cat /var/lib/graphx/runtime/package-versions.tsv
       df -h /var/lib/graphx
     '
   ```

   Require Linux/aarch64, Node 24, all listed tools, working guest Docker/Compose/
   Buildx and OVS, matching guest fingerprint, and at least 10 GiB free in the guest.
   Record missing packages or mismatches as recovery failures. Recheck host source
   status and host free space. Do not run `infrastructure/lima/verify.sh` here:
   it exceeds environment readiness and includes privileged qualification.

## Failure handling and handoff

Before deletion, stop and retain the original stopped instance on any failed gate.
After deletion, preserve the external backup and new failure logs. Restoring the
old disk/configuration requires a separately reviewed recovery action; it would
still have the stale identity and must not be passed off as a current environment.
Do not merge old Docker state or ownership records into the replacement.

Successful recovery means a current, identity-checked VM with readiness evidence,
not P3/P4 acceptance. Report backup location and verification, new VM identity,
provisioning/package evidence, remaining capacity, and failures. Then return to
runbook steps 2–3 for staging review and obtain separate authorization for step 4.
No acceptance claim changes merely because the VM is ready.

## Suggested authorization

> Authorize the scoped recovery in `design/four-radio-vita/lima-recovery-proposal.md`.
> Use external backup volume `<absolute mounted volume path>`. Preserve and verify
> the stopped graphx instance before deleting it. Only after all preservation and
> identity gates pass, recreate graphx using the repository launcher and perform
> the specified environment readiness probes. Retain the backup. Do not run P3/P4
> acceptance, stage/start graphs, publish images, push or deploy. Stop on any failed
> gate or changed identity.

This proposal was checked against installed Lima 2.2.0 CLI help, local provisioning
and identity-check code, and read-only host metadata. No backup, VM mutation,
provisioning, guest inspection or privileged acceptance was performed to prepare it.
