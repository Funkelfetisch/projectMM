# HELIO controls responsive layout

## Intent

The HELIO Controls panel uses its available dashboard width without clipping controls. Advanced scene groups form two columns when the viewport supports them and collapse to one column on narrow screens.

## Changes

1. Remove the local content-width cap from the Controls panel and carry the container width through every nested grid.
2. Give slider, select, and color rows shrinkable columns and a narrow-screen layout that moves the range input below its label and value.
3. Keep the existing Controls disclosure and Advanced scene controls disclosure, with 44 px minimum interactive rows and visible keyboard focus.
4. Verify JavaScript syntax and render the dashboard at desktop and narrow viewport widths before device use.
