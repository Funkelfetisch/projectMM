# HELIO hardware output controls

## Intent

The HELIO dashboard exposes every output transform that changes the physical strip: driver brightness, output curve, pixel order, and white mixing. The HELIO boot profile uses the demonstrated GRBW wire order and linear output curve.

## Alternatives

1. Leave these controls in the generic module editor. This hides physical output behavior behind a separate view during normal HELIO use.
2. Add the four output controls to the existing dashboard Controls panel. This keeps product operation in one place and reuses the driver's standard controls and validation.
3. Add HELIO-only RMT controls. This duplicates the driver's correction model.

Option 2 keeps the existing driver as the sole owner of output behavior.

## Steps

1. Set the HELIO boot default to GRBW, accurate white mixing, and a linear output curve.
2. Make Restore HELIO defaults restore the same driver configuration.
3. Render the existing RMT correction controls in the HELIO dashboard.
4. Build and verify the configuration on the 222-light controller.
