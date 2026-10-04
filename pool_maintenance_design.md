# Pool Maintenance — design

The one correct version of Pool Maintenance's design, the screen, and the RAM exchange. Every earlier version, spec, branch and past commit of these is stale.

## What it is

- **An OS layer of the one system.** Being OS means holding defined authorities, not being independent. It takes instruction from the rest of the system.
- **Its function is managing pools.** It is the only place a pool comes into or goes out of existence. Owning the RAM for pools, including every model's KV cache (the tokenised form of a pool's contents), is part of that one function, because safe destruction would otherwise have to go through other systems.
- **Authorities:**
  - pool-level access over every pool and every model's KV cache
  - who holds the pool map screen on LiveRegistry
  - which bytes go back when RAM is reclaimed
  - when a destruction it is told to carry out is safe, and carrying it out then
- **Not held:** whether a pool exists or is destroyed. That arrives as an instruction.
- **The RAM Manager:** co-dependent. It supplies RAM, Pool Maintenance decides how it is used and what is returned. Neither sits above the other.

## Boot

Boot is not Pool Maintenance's process. Pool Maintenance has one thing to do when it happens: claim one continuous block of 80 GiB from the RAM Manager, 1 GiB designated RAM and the rest VRAM, stated in the one request. Whether the claim stands goes to Wellness.

- **The RAM part** holds the map and every preserved section.
- **The VRAM part** is pool memory: pools and KV caches.
- Everything Pool Maintenance uses comes from its own 80 GiB.

## The RAM exchange

- Either side can ask the other for RAM, directly, in both directions.
- **When Pool Maintenance asks:** nothing is tracked. On a spawn or grow, if its free space cannot cover what is needed, it asks for the shortfall and the spawn or grow goes ahead. At this scale (around ten pools of about 2 MB, plus KV caches, against 79 GiB) this almost never happens.
- **The RAM part:** if it has no room for a preserved section, Pool Maintenance asks the RAM Manager for that room and the edit goes ahead. Nothing is refused or stopped.
- **When the RAM Manager asks:** when it cannot cover what it needs. Pool Maintenance chooses which bytes go: from its most recently received stretch first, cutting from the end, so the boot block is touched last. It never gives up bytes in use or its RAM part.
- **Every exchange is a flag to Wellness, from both sides.** Pool Maintenance needing more than 80 GiB is a warning, and everything else outgrowing 48 GiB is a red flag. The exchange still happens.
- Continuity is required of the boot claim only. The exchange should rarely be needed, but it must always be possible.

## The screen

- **Claimed when LiveRegistry starts up, whenever that is.** LiveRegistry is not OS and does not start with boot. On starting up it tells Pool Maintenance, which claims the screen then.
- **The screen is a reflection.** It names the map's real memory in Pool Maintenance. The map never leaves Pool Maintenance and nothing is copied to LiveRegistry. It is not the map itself, and not an address handed to readers.
- **Reading:** a reader holds the screen and resolves each section through the reflection, reading the bytes it lands on. Normally those are the map's real bytes. Holding the screen is the arrival and letting go is the leave. The reader asks and tells nothing, and nothing depends on it being honest.
- **Reading is always permitted.**
- **An edit to one section (one pool's entry):**
  1. **Preserve.** The section's pre-edit contents go into a separate piece of the RAM part, untouched.
  2. **Redirect.** For as long as the edit is in progress, the section resolves to the preserved bytes, for every reader.
  3. **Edit.** The real section is edited in place, at full pace.
  4. **Lift.** The instant the edit finishes, the redirect is removed and the section resolves straight to the real bytes again. Nothing is pointed at anything new; the redirect stops.
  5. **Free.** The preserved section is freed once the last reader that landed on it lets go.
- No one sees an edit in progress. Readers never wait and edits never wait. A preserved section exists for one edit only, never as a standing second copy.
- **No pools, no map.** With no pool standing the screen shows nothing, and that is correct, not a failure.

## A pool

- **A requested pool takes 1 MiB of Pool Maintenance's own VRAM part:** a freed one if there is one, else the next. With none left, the 1 MiB is asked for from the RAM Manager (Wellness told, both sides) and the spawn goes ahead. 1 MiB is around a quarter of a million tokens of text, far beyond an average exchange; growing is for the rare large case.
- **Its stamps** are put on it.
- **Its barrier** is put around it.
- **Its KV sections** come out of Pool Maintenance's memory, and each one gets the pool's barrier. How many sections there are, per model or per agent, is KVBuilder's business, not Pool Maintenance's.

## The pool-level barrier

- Pool Maintenance puts a pool-level barrier around each pool and around each KV section representing that pool. It is a permission it never asked for, almost always granted automatically. Readers may not even know they hold it.
- Because every entry passes the barrier, Pool Maintenance always knows who is reading.
- When a pool is marked for destruction, no new entry to the pool or its KV cache is allowed. When the last reader leaves, both are destroyed together.

## Header rules

None while the file is being built. They are written fresh and complete once the file is whole.
