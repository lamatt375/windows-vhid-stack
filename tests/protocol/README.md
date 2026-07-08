# Protocol Tests

Future protocol validation should cover the fixed no-payload smoke trigger boundary: unknown IOCTLs, nonzero input/output buffers, disconnected device, not-ready state, already-running state, already-completed state, and cleanup/deletion races.

No arbitrary write-capable command protocol exists. The current control surface is one fixed trigger for the built-in smoke sequence.
