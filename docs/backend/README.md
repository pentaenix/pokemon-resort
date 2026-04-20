# Pokemon Resort Backend Guide

This directory is the backend contract for agents building Resort storage, transfer, and frontend flows.

The current backend supports:

- import-grade Pokemon reads from external saves through the PKHeX bridge
- canonical Resort Pokemon storage in SQLite
- independent box/slot placement
- raw snapshots and history/audit events
- conservative create-vs-merge matching
- managed mirror sessions for outbound projections
- return-aware matching for managed mirrors
- native export projection snapshots
- a backend seed/export command for integration testing

Start here:

- [storage_model.md](storage_model.md) describes canonical Pokemon, boxes, snapshots, history, and mirror tables.
- [api_reference.md](api_reference.md) lists existing services, methods, repositories, bridge commands, and tools.
- [import_flow.md](import_flow.md) describes external save import and return import.
- [export_flow.md](export_flow.md) describes projection, mirrors, and the guarded write-back boundary.
- [frontend_integration.md](frontend_integration.md) tells UI agents what services and read models to use.
- [testing_and_seed_data.md](testing_and_seed_data.md) documents test commands and seed workflows.

Do not build UI against transfer-ticket summaries or sprite slugs as durable Pokemon records. They are preview data only.
