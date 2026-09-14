# GraphX documentation

The documentation is organized into two books. Start with the user guide to run
GraphX, or the architecture document to review how the system works.

| Book | Contents |
|---|---|
| [User guide](user-guide.md) | Installation, example selection, console and CLI, configuration, execution, observability, history, capture, scenarios, QEMU, release administration, troubleshooting and glossary. |
| [Architecture](GraphX_Architecture.md) | Components and deployments, configuration contracts, ownership and lifecycle, networking, transports and protocol, telemetry, credentials, security, failure handling and the review checklist. |

For a first run, use the [example quick start](../examples/quick-start.md).
The [example matrix](../examples/README.md) lists every authored example and its
supported targets. Environment-specific setup stays with the
[Lima infrastructure](../infrastructure/lima/README.md) and [guest sources](../guests/README.md).

## Supporting records

These records remain separate because they govern changes, record evidence or
provide a versioned compliance inventory:

- [Project decisions](project-decisions.md): accepted constraints for contributors.
- [Test procedure](test-procedure.md): verification environments, authorization and test families.
- [Release license inventory](release-license-inventory.md): dependency and redistribution inventory.
- [Documentation verification](documentation-verification.md): checked scope and evidence limitations.

The [review checklist](GraphX_Architecture.md#architecture-review-checklist) is part
of the architecture book. The [manual acceptance checks](user-guide.md#manual-acceptance-checks)
are part of the user guide.
