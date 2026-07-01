# Proof Client

This directory is reserved for a future VM-only proof client.

Current boundary:

- does not open a driver handle;
- does not send IOCTLs;
- does not move/click/type;
- does not prove driver effects.

Any write-capable proof client must wait for descriptor validation, readiness checks, clear-all design, and explicit VM-only write-proof approval.
