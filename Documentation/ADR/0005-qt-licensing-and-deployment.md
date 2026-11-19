# ADR-0005: Qt licensing and deployment

- **Status:** proposed. The owner has **deferred** the license decision for K Engine itself (2026-11-19: "not needed for now"); it blocks nothing until binaries are distributed. Re-open this ADR before the first distribution.
- **Date:** 2026-11-18

## Context
The Editor is planned on Qt 6. Qt's license terms constrain how the Editor can be distributed. The K Engine project license has **not** been chosen, and no `LICENSE` file exists on purpose.

## What is NOT verified
No Qt files, license texts or upstream pages were available (no network). The points below are recollection and **must be checked against Qt's official licensing documentation and the license files of the exact modules and version used before any distribution**.

## Recollection to verify
- Qt is offered under commercial terms and under open-source terms (LGPLv3 and/or GPLv3 depending on module); some add-on modules are GPL-only or commercial-only.
- LGPL use generally implies dynamic linking, shipping the license text and notices, and allowing the user to relink/replace the Qt libraries.
- Static linking Qt has stricter implications and should be avoided unless counsel approves.

## Options
1. Use only Qt "essentials" modules that are available under LGPL, link dynamically, ship notices. Keeps the engine's own license flexible.
2. Buy a commercial Qt license.
3. Choose a different GUI toolkit (large cost; not warranted now).

## Decision (proposed)
Option 1 by default: restrict the Editor to a minimal, documented module set (Core, Gui, Widgets and the OpenGL widget module are the expected ones — to be confirmed), dynamic linking only, **Editor-only** dependency. The standalone Runtime and `Engine/*` must not depend on Qt. Record the exact modules and their licenses in `DEPENDENCIES.md` once Qt is installed and inspected.

## Consequences
- Any module beyond the approved set needs a license check first.
- Deployment (installers/bundles with the Qt libraries) is a task for M4 and requires a license compliance checklist.

## Open questions for the owner (deferred, not blocking development)
1. Which license will K Engine itself use (affects compatibility with LGPL/GPL components)?
2. Is a commercial Qt license an option?
3. Is the Editor intended to be redistributed as binaries?

## Validation plan
Before first binary distribution: enumerate linked Qt modules (tooling output), read each module's license file, produce a notices file, have the owner approve.
