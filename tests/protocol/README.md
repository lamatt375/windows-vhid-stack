# Protocol Tests

Future protocol validation should cover the fixed no-payload smoke trigger boundary, read-only status boundary, fixed `MoveAbsolute` boundary, and fixed left-only `ClickAbsolute` boundary: unknown IOCTLs, trigger nonzero input/output buffers, status nonzero input or missing output buffer, exact command request size, version and command type checks, reserved-field rejection, out-of-range coordinate rejection, disconnected device, not-ready state, already-running state, already-completed smoke trigger state, and cleanup/deletion races.

No raw HID payload API, queue, macro language, arbitrary button command, key-tap, or text protocol exists. The current control surface is one fixed trigger for the built-in smoke sequence, one read-only fixed status query, one typed normalized absolute movement request, and one typed normalized left-click request.
