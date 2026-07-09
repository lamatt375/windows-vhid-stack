# Protocol Tests

Future protocol validation should cover the fixed no-payload smoke trigger boundary and read-only status boundary: unknown IOCTLs, trigger nonzero input/output buffers, status nonzero input or missing output buffer, disconnected device, not-ready state, already-running state, already-completed state, and cleanup/deletion races.

No arbitrary write-capable command protocol exists. The current control surface is one fixed trigger for the built-in smoke sequence plus one read-only fixed status query.
