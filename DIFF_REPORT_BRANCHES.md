# Branch comparison — `fervent-planck` (latest) against `focused-bohr` (latest of the other branches)

## What is being compared, and why this branch

`focused-bohr` is the most recently touched of the other branches (28 Sep, 14:49). But it split off from the very first upload: its Pool Maintenance, Live Registry and Watcher are still the **original uploaded versions**, plus one extra document. Everything that happened afterwards happened on `fervent-planck`. So this comparison reads as "the originals" against "where the work is now".

(`upbeat-cray` and `main` are older stops on the same road; they add nothing new.)

Two things about the other branch's files:
- It holds **two copies each of Live Registry and Watcher**: a plain-named one and a "(2)" one. The "(2)" copy is the later of the two (it already has the pool map pointer and the newer rules wording). I compared against both. There is no plain-named Watcher on that branch at all, only "Watcher (2)".
- It has a **File Controller** file and a spec called *incremental generation — the Coding Turn*, both of which the newer branch no longer has.

## The one item that is only on the other branch

**The Coding Turn document.** It describes one long-lived turn that stays open across as many write-and-check rounds as a feature needs, only one at a time, which switches the ordinary turn off completely while it runs. It exists only on `focused-bohr`. No code anywhere depends on it. If it still stands, it has to be carried across by hand; nothing else is lost.

## Whole files that are gone from the newer branch

- **File Controller** (both its files). Its whole job was the "which source file did this pool come from" tag. It read and wrote the tag table on the Live Registry and never touched pools itself. With the table removed everywhere, it has nothing to do, so it went too.
- The "(2)" duplicates of Live Registry and Watcher (the newer branch keeps one copy of each, under the plain name).
- The old Live Registry ("-old") files were not deleted; they lost the same file-tag table and its five actions, and nothing else.

## Live Registry

1. **What it says it is.** The old opening statement was a list of everything it holds (pipeline name, roster, pool table, temperatures, rebuttal switch, prompt links, pool-file tags) plus the permission key. The new one is a short general statement: the single collection of active system state, read by many, "the noticeboard for current state", which does not take part in anything. The list of held things is gone from that statement. The permission key section itself is still in the file.
2. **The rules preamble** now uses the standard strict wording, not the "fixed points" or "during development" wording. Rule 6 was shortened: the old text excused the permission key and the two fixed class slots as permanent, and that excuse was removed.
3. **Pool-file tags removed.** The table, its lock, and the five actions on it (set, look up, list-by-file, clear one, clear all for a file). Nothing on the Live Registry now records where a pool's content came from.
4. **The screen changed nature.** In the "(2)" copy the screen was a pointer that pointed either at the live pool map, or at a full photograph of the whole map taken before every edit. That is gone: no whole-map photograph exists anywhere. The screen is now simply where the map lives and is read, and the writing rules are in Pool Maintenance.
5. **A written build outline** (marked "to be removed once the OS is built") was added under the screen, describing how the map is hosted, read, left, and cleaned up, including the new step about the key.
6. Small wording fixes: "the pipeline is simply all it happens to hold so far" became "the pipeline is one of the things it holds".

## Pool Maintenance

Same job (make, grow, reclassify, protect, remove pools), but it has become a different kind of file.

**What it owns**
- **Before:** it kept its own private list of pools and which memory blocks belonged to each.
- **Now:** it keeps nothing. The pool map is one fixed region of equal-sized slots, one pool per slot, living on the Live Registry's screen. Pool Maintenance is the only writer.
- Memory now comes from the OS in uniform "chunks" instead of "blocks" from a memory allocator. The old file's dependency on that allocator is gone.
- A pool no longer carries a "how far written" marker. The old text said content and that marker were written by whoever was generating; the new text says only content.
- A pool's list of prompt ids used to be an ordered list; it is now an unordered set with no repeats.
- Each pool gained a "flagged for destruction" mark.

**How editing works (the main change)**
- Before, an edit just changed the pool while readers might be looking at it.
- Now every edit is **copy-on-write, slot by slot**. If no one is looking at a slot it is changed in place. If readers are looking at it, the old version is copied once into a "stash", those readers keep seeing the stash, and the change is made live. When the last reader of a stash leaves, it is released immediately. An edit never waits for a reader and readers never wait for an edit. There is never a copy of the whole map.

**How reading works (new)**
- Every read of the map, and of a pool's contents, is now granted by Pool Maintenance: a reader **arrives**, and **leaves**. A read cannot be handed on, stored, or kept beyond its reader. A reader that dies without leaving is left for by the system.
- Then, in the latest commit: readers get the **map key** first (see the other report).

**How removal works (changed)**
- **Before:** destroying a pool freed its memory and removed it at once.
- **Now:** destroy marks the pool and refuses new arrivals; readers already inside carry on; when the last leaves, the pool goes and its chunks return to the OS. With no readers, it goes at once.
- Once a pool is marked for destruction, grow, flag, unflag and reclassify all refuse it, and a new pool cannot continue from it.

**Behaviour that disappeared**
- **Shrinking a pool** (handing back memory beyond a chosen size) is gone. Nothing replaces it.

**New behaviour**
- Making a pool now needs a free empty slot. If there is none, it refuses and tells Wellness ("map full").
- Wellness is also told when a reader dies while still holding an old stash, when a stash is made (information only), and when an arrival on a pool is refused.

**Unchanged in what it does:** the matching by class, turn, prompt or pool id, the exclusion list, source-based immunity, and resolving a class by number or name.

**The written rules**
- Rule 3 changed from "never writes anything to the Live Registry" to "never writes to the class table", because the map now lives there.
- Rule 5 changed from "this file is not a directory" to "this file holds nothing".
- Rule 8 gains "nothing is reverted", rule 10 moves from allocator blocks to OS chunks, and rules 11 to 14 are new: copy-on-write per slot, this file grants every read, destroy alone ends a pool and only after the last reader leaves, and the map is one contiguous region.
- The preamble is the standard strict wording.
- Review markers named "COW-EDIT" are scattered through the files. The file itself says they are to be stripped after review.

## Watcher

**How it reads the pool map**
- **Before ("(2)"):** it looked at the map through a pointer that was either the live map or a photograph of it, copied every pool record out in one go, and left. If the pointer was empty, it skipped the pass and told Wellness.
- **Now:** it asks Pool Maintenance for a read, asks once per session for the key, copies each slot's record out through the key, keeps the read open while it reads pool contents, and leaves at the end of the pass. The empty-pointer case is **no longer handled**: the code goes straight to the map.
- Reading a pool's contents is now done inside a granted read on that pool. A pool refused (marked for destruction, or gone) is simply not read.

**Things that changed that are not about the map**
- **Shared searches (new).** Before, every request searching pool contents did its own search. Now each distinct search is done once per pass across all pools and every request that asked for it takes that one result. The stop-word search is always among them, and the "add to the forbidden list" step uses that same result.
- **The "was it read this pass" markers were removed.** A source is now treated as "not read" if it is empty and has never been read, instead of a separate remembered yes/no.
- **The rules text for reading (Ruling 9) was shortened.** The old text listed what Watcher can read (pool map, a pool's contents off VRAM, RAM, VRAM, a file's flag, a file on disk) and said new types would be added there when needed. The new text says only that every source is read whole once per pass. **That list of readable things no longer appears anywhere in the rules.**
- Ruling 5 lost its sentence naming the OS as the reader of the forbidden list. Several notes saying "the OS will do this" or "nothing sends this yet" were removed.
- It may now also hold the map key (the previous report).

**Unchanged:** the queue, doors, forbidden list, sleep and wake, triggers and checks.

## Things to be aware of (raised once, not blockers)

1. Watcher's Ruling 9 no longer lists the sources it can read.
2. The "screen not there" safeguard in the Watcher was removed rather than replaced.
3. Shrinking a pool has no replacement in the newer Pool Maintenance.
4. The Coding Turn document exists only on `focused-bohr`.
