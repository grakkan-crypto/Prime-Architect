# The pool map screen

Reference for an instance working on Pool Maintenance, LiveRegistry, or any reader of the pool map. It states what the screen is and how it works. Any code, header, spec or past version that differs from this is wrong.

## Parties

- **Pool Maintenance**: owns the pool map. The map is private to Pool Maintenance. It is never handed out, never copied, and its location is never given to anyone.
- **LiveRegistry**: carries the screen.
- **Readers**: every party that reads the pool map (for example Watcher, ContextMatcher). Readers are not parties to the screen mechanism. Nothing in the mechanism requires anything of them beyond reading the screen.

The mechanism involves Pool Maintenance and LiveRegistry only.

## What the screen is

- The screen is a reflection of the map's own bytes: the same physical memory as the map, made visible at a second place, on LiveRegistry.
- It is not a copy. There is one set of bytes, visible in two places. Nothing is copied to the screen and nothing is kept in step with anything.
- It is not an address, pointer, handle or reference to the map. The screen does not tell a reader where the map is. A reader reading the screen is reading the screen's bytes, which are the map's bytes.
- On the screen the bytes are read-only. Only Pool Maintenance, in its own place, changes them.
- A change Pool Maintenance makes to the map is visible on the screen at the same instant, because they are the same bytes.

## Reading

- The screen is the only place the pool map is ever read from by anyone not making an edit.
- A reader reads the screen through the map key (the layout of the map: unit count, unit size, and the byte offset of each field within a unit), which it asks Pool Maintenance for once, before its first visit of the session.
- A reader never calls, asks or tells Pool Maintenance or LiveRegistry anything in order to read. There is no registration, announcement, ticket, holder object, or arrive/leave call for reading the screen. The act of reading is the arrival; ceasing to read is the leave. That act alone is all that is needed.
- A reader is never redirected. It is never sent to read anywhere other than the screen.
- Reading is always possible. A reader never waits.

## Editing

An edit touches only the units it changes, one unit at a time:

1. **Build apart.** The unit's complete new content is written into separate memory. Nothing on the screen and nothing a reader can see is touched while it is built. Content is never written once it can be read.
2. **Switch.** What the screen shows for that unit is switched from the old bytes to the new bytes, in one atomic step. There is no moment at which the unit shows part old and part new.
3. **Old stays.** The old bytes are left exactly where they are, unchanged, never copied.
4. **Release.** The old bytes are released once no read begun before the switch can still be on them. This is known from the act of reading itself, never from anything a reader announces.

Result:

- A read begun before the switch sees the old unit, whole.
- A read begun after the switch sees the new unit, whole.
- No edit waits for a reader. No reader waits for an edit. No edit checks for permission or holds.
- There is no whole-map copy, snapshot, freeze or second map anywhere.

## No pools

The map is the pools. With no pool standing, the screen shows nothing. That is correct: not a failure, not an empty state to repair, and nothing is placed there to fill it.

## Never

Each of the following is wrong and is removed wherever found:

- Giving readers, or LiveRegistry, the map's location so that they read the map directly.
- The screen holding an address of the map, or of anything in it, instead of reflecting the bytes.
- Redirecting readers to anything: a preserved section, a copy, a static image, or any other memory.
- Preserving a unit's old content as a copy and then editing the live unit in place.
- Any copy of the map or any part of it, held anywhere, for any length of time, as the thing readers read.
- Readers announcing, registering, counting themselves, or holding an object in order to read the screen.
- Counting or tracking readers of a redirect or of a copy as a way of knowing who is reading.
- Any change to readers made to serve the screen mechanism. Readers read the screen; the mechanism is Pool Maintenance and LiveRegistry alone.
