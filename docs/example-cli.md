# Example CLI

`graphx example` is the common workspace interface for the authored examples.
It calls the authoritative C++ loader/compiler, verified release builders and
existing `graphx run` ownership lifecycle. Python 3 and a source checkout are
required for preparation; no additional configuration parser is introduced.
`graphx config authored FILE` exports the loader-validated authored object as JSON.

| Command | Behavior |
|---|---|
| `graphx example list` | List authored examples |
| `graphx example plan NAME` | Report target, nodes, paths and required artifacts without mutation |
| `graphx example prepare NAME` | Prepare artifacts and compile without starting |
| `graphx example up NAME` | Prepare if needed, start and open an authenticated console |
| `graphx example open NAME` | Reopen an authenticated console without restarting |
| `graphx example status NAME` | Verify ownership and report resource status without tokens |
| `graphx example tokens NAME` | Verify ownership and retrieve current staged tokens |
| `graphx example logs NAME --node NODE --follow` | Follow application logs |
| `graphx example down NAME` | Stop through the common ownership lifecycle |
| `graphx example scenario NAME --action ID` | Run an explicit declared scenario action |
| `graphx env doctor` | Check CLI, Docker/Compose and macOS Lima inventory |
| `graphx env up` / `down` | Explicitly start/stop the GraphX Lima environment on macOS |
| `graphx verify PROFILE` | Run an existing verification profile |
| `graphx release TOOL …` | Run the existing release tool with its original arguments |

Release tools are `native`, `images`, `platform`, `guests`, `install`, `verify` and
`publish`. Release publication remains an explicit command. For example,
`graphx release images verify DIR` verifies an existing image candidate.

Common options:

- `--source CHECKOUT`: source repository, otherwise discovered from the working directory.
- `--target TARGET`: explicit supported placement; default is native Linux on Linux,
  or OrbStack/native macOS/Lima according to the authored execution model on macOS.
- `--images DIR`, `--release DIR`, `--catalog DIR`, `--external DIR`: existing artifact and external
  credential inputs. Paths are guest-local when targeting Lima from macOS.
- `--control NODE:pause,resume`: explicit operator grant on a working copy; repeat
  for additional nodes. Add `--control collector:reset` to authorize Reset counters.
  Reset clears collected metrics; it does not restart applications or erase history.
- `--allow-privileged`: required for OVS/guest work and its ownership checks.
- `--laboratory ID`: explicit pre-start laboratory substitution.
- `--restart`: stop the current owned run before restarting or replacing its compilation.
- `--instance NAME`: separate launch reference and graph identity.
- `--workspace DIR`: local workspace root; defaults to `outputs/examples` on macOS
  and `/var/lib/graphx/examples` on Linux. Lima runtime state always stays in the guest.
- `--no-open`: skip browser opening and return the URL and tokens for manual use.
- `--operator REF`: select one existing control credential for browser login when multiple are declared; otherwise login is observation-only.
- `--json`: suppress browser opening and return a machine-readable result; progress is sent to stderr.
- `--operation plan|run|status|clear`: operation for an explicit scenario action.

Working copies contain the selected authored graph, including any control grant.
A private `current.json` stores only generation and artifact references, with no
tokens. It is not a deployment configuration. The compiler's manifest and the
runner's ownership ledger remain authoritative. Launch and artifact locks prevent
concurrent conflicting preparation. Source changes require an explicit restart.
Each compilation generation gets a distinct graph identity, preserving prior history.

Lima dispatch checks the existing VM's repository identity, copies a source
snapshot without macOS metadata, builds a guest-local CLI and uses the same
workflow there. It never provisions a VM as a side effect of `example up`.
The host keeps a reference to the snapshot so status and cleanup use the original
runner after source changes. The standard console forward is guest 8080 to host
18080; other ports require an explicit environment forwarding configuration.

`up --no-open`, `up --json`, and `tokens` return the observation credential and any enabled control
credential. Interactive `up` and `open` instead establish a browser session and omit
the tokens from terminal output. With multiple authored operator grants, `control_token` is a mapping
from credential references to tokens. Disabled control returns null. The CLI
never grants access merely because a node supports control. Explicit generated
operator tokens are staged through the normal external credential provider.

See [quick start](../examples/quick-start.md), [execution](execution.md) and
[release process](release-process.md) for prerequisites and lower-level contracts.

For QEMU, `logs` defaults to the platform telemetry log. Raw QEMU boot logs remain
in the runner-owned guest directory; `--node` selects container or native process
logs. A supplied combined guest installation needs its matching `--catalog DIR`.

## Browser authentication

The local CLI authenticates to the loopback console with the current staged tokens
and obtains a single-use login code valid for 60 seconds. It opens that code in
a URL fragment, which the page removes before exchanging it for an HttpOnly,
SameSite=Strict cookie. Tokens are never embedded in page assets or returned by
a public bootstrap endpoint. HTTPS sessions also set Secure.

Sessions are bound to one graph, console origin and existing credential scope,
expire after eight hours, and are revalidated against the underlying credentials.
Revocation and platform replacement invalidate them. Refresh keeps the session;
use `graphx example open NAME` to authenticate again. Cookie-authenticated control
requests require a session CSRF header and an allowed Origin for mutations.
HTTP observations and WebSocket streams use the same session. Manual bearer
authentication remains available under **Manual authentication**, and retains
precedence when supplied explicitly.

On macOS, browser opening and login happen on the host even when the example runs
in Lima. Automated jobs should use `--json` or `--no-open`. If browser opening
fails, the started example remains running; retry `open` or retrieve `tokens`.
No control permissions are added by browser login.
