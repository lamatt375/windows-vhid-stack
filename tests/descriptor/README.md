# Descriptor Validation

`validate_descriptor.py` parses `src/shared/VhidHidDescriptor.h` and checks
static descriptor invariants without building, installing, or loading the
driver.

Run from the repository root:

```powershell
python tests\descriptor\validate_descriptor.py
```

The validator checks balanced collections, Report IDs 1 and 2, keyboard and
mouse top-level usages, and absence of Output or Feature reports. It does not
open driver handles, submit HID reports, or generate input.
