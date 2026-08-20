# Branded UI first paint

## Goal

Prevent the generic projectMM shell, logo, and colors from appearing briefly while a branded device UI starts.

## Design

- Mark the document as booting in the initial HTML and keep the application shell visually hidden with a small inline critical style.
- Keep the background neutral while the stylesheet, module script, and first device-state snapshot load.
- Apply device branding before rendering the first state snapshot, then reveal the complete UI on the next animation frame.
- Remove the boot gate after a bounded timeout so a failed script or unavailable state endpoint never leaves the controls permanently inaccessible.
- Pin the ordering and fallback in the host-side UI tests, then verify repeated reloads against HELIO hardware.

## Scope

The change stays in the existing embedded UI startup path, its specification, and its host-side tests. It adds no firmware module or platform behavior.

## Outcome

The embedded UI starts with a neutral hidden shell. Both initial state paths apply branding and render the complete view before the first visible application frame. Four consecutive hardware reloads show the neutral boot state first and the HELIO brand only after state arrival, with no projectMM-branded intermediate state.
