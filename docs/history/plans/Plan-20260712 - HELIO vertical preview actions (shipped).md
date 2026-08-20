# HELIO vertical preview actions

## Intent

Keep the main HELIO preview visually dominant while placing its four direct actions in a compact vertical rail beside it.

## Plan

1. Preserve the existing action order and behavior.
2. Stack the action controls vertically in the preview stage.
3. Keep the rail beside the preview down to tablet widths, then collapse it above the preview on narrow screens.
4. Pin the layout with a browser-side source test and verify desktop and mobile rendering before flashing the controller.

## Scope

This changes only the HELIO preview-stage layout, its cache token, and the matching UI test.

## Outcome

The four controls form an equal-width vertical rail beside the square preview. The stage collapses to one column at 720 px, and the live device layout is verified without overlap.
