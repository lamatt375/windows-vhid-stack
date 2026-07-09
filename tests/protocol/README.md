# Protocol Tests

Future protocol validation should cover the fixed no-payload smoke trigger boundary, read-only status boundary, and the fixed `MoveAbsolute` boundary: unknown IOCTLs, trigger nonzero input/output buffers, status nonzero input or missing output buffer, exact `MoveAbsolute` request size, version and command type checks, reserved-field rejection, out-of-range coordinate rejection, disconnected device, not-ready state, already-running state, already-completed smoke trigger state, and cleanup/deletion races.

No raw HID payload API, queue, macro language, click, key-tap, or text protocol exists. The current control surface is one fixed trigger for the built-in smoke sequence, one read-only fixed status query, and one typed normalized absolute movement request.
