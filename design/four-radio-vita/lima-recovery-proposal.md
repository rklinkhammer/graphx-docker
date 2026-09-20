# GraphX Lima recovery status

The dedicated `graphx` VM is running with the required provisioning identity:
`995153b13806694ef031f1b07218f9eafd2c31043deb33457504497cb153339e`.
No environment recovery or external backup is pending. The user selected a clean
recreation without backups and authorized the privileged P3/P4 matrix.

Use the [operator runbook](privileged-verification-runbook.md) for identity checks,
fresh image preparation and owned fixture cleanup. Build images without cache
once per verification run and create new containers for each case. Do not delete
or reprovision the VM to work around a test failure, alter its fingerprint, or
restore ownership records from another environment.

All 18 P3/P4 cases pass in the current Lima environment. See [P3 verification](p3-verification.md) for the evidence and limits.
