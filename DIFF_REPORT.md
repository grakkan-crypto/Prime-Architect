# Diff report — Pool Maintenance, Live Registry, Watcher

## Which versions were compared

Only one branch holds the newest work: `claude/fervent-planck-rxlx93`. The other branches (`main`, `focused-bohr`, `upbeat-cray`) are all older.

- **Latest version:** commit `4ebb89e` (28 Sep, 08:27) "Map key: Pool Maintenance hands out how to read the screen; readers ask once per session".
- **One version before:** the state immediately before that commit (`f0995ea`). Earlier that morning a couple of changes were made and then undone, so this "before" is identical in content to the state at `67a1e0a`.
- Pool Maintenance, Watcher and the Live Registry header changed in the latest commit. **The Live Registry code file did not change in it.** Its own latest change was `68ec0d4` (27 Sep), so that one is reported separately in Part 4.

## The one-paragraph summary

Before, anything that read the pool map (the "screen") — the Watcher — looked at it directly, knowing exactly how Pool Maintenance had laid it out. Now Pool Maintenance hands out a **key**: a sheet saying "the thing you want is this many steps in from the start". A reader asks for the key once, at the start of a session, keeps it, and only ever reads the screen through it. The point: if Pool Maintenance rearranges its map later, only Pool Maintenance changes; readers just get a new key.

---

## Part 1 — Pool Maintenance, description file (60 lines touched: +52 / −8)

**1. Added lines, in the rules text, ~line 158–160 (3 lines):**
"THE KEY. Before its first visit of the session, a reader asks this file for the map key and keeps it. It reads the screen only through the key."
→ A new written rule. Anyone reading a pool map must first ask Pool Maintenance for the key, keep it, and never read the map any other way.

**2. Removed lines ~275–283 (the old "how a reader sees a unit" helper, 7 lines).**
This was a small shared routine that took one slot of the map plus a reader's place in time and answered: "which pool record does this reader see here?" It did so by reaching straight into the map's inner layout: if the reader arrived after the slot's last change, show the current pool (or nothing if empty); otherwise walk back through the saved older copies until you find the one that covers the reader's moment.
→ Gone, because reading the layout directly is exactly what is no longer allowed.

**3. Added lines ~279–297 (the key itself, 17 lines).**
A new form called the map key. It is a list of number slots, each one a distance in bytes from a start point. It is grouped in four parts:
- the map as a whole (where the slots start, how many there are, how big each is);
- one slot (is a pool present, when it last changed, where its older copies hang, where the current pool sits);
- one older copy (is a pool present, the time span it covers, where its content is, what comes before it);
- one pool record (its id, class, turn, prompts, time stamp, size, and whether it is marked for destruction).
→ This is the "sheet of directions" that replaces knowing the layout.

**4. Added lines ~299–303 (5 lines).**
A helper that reads one value at a given distance from a given point, treating it as the kind of thing the key says it is.
→ The basic "go this far in and read this" move.

**5. Added lines ~305–309 (5 lines).**
A helper that finds slot number N on the screen using the key (start of the slots + N × slot size).
→ Finding a slot without knowing the layout.

**6. Added lines ~311–325 (15 lines).**
The replacement for the removed routine in item 2. It does the same job with the same logic — current pool if the reader is up to date, otherwise the matching older copy, or nothing — but every look-up goes through key distances instead of named parts of the layout. It returns the pool record's position rather than a typed pool.
→ Same behaviour, different route: nothing about *what* a reader sees has changed, only *how* it finds it.

**7. Added lines ~342–345 (4 lines), in the public list of things Pool Maintenance offers.**
"The map key, whole. Asked for once per reader per session, before its first visit to the screen." plus the request itself.
→ A new thing readers can ask Pool Maintenance for.

## Part 2 — Pool Maintenance, working file (28 lines added, none removed)

**8. Added line ~12:** a standard tool is brought in for measuring distances inside a structure. Needed by item 9.

**9. Added lines ~142–167 (26 lines):** the code that builds and hands out the key. For every slot in the key, it measures the real distance in Pool Maintenance's own map and writes it in. The number of slots is read from the screen itself, and the slot size is measured.
→ Because Pool Maintenance measures its own layout at the moment it makes the key, the key is always truthful. If the layout changes, the key changes with it and no reader needs editing.

## Part 3 — Live Registry description file (4 lines added)

**10. Added lines ~301–304:** in the written procedure for readers of the screen, a new step "1a — THE KEY" is inserted: before its first visit of the session, a reader asks Pool Maintenance for the map key, keeps it, reads only through it, and a change to the map's layout is a change to Pool Maintenance alone.
→ Documentation only; the Live Registry's working code did not change.

## Part 4 — Watcher (44 lines touched)

### Description file (+11 / −2)

**11. Changed lines ~22–25:** in the statement of what the Watcher is allowed to hold, "…the requests it refused, kept for Wellness. Nothing else." becomes "…the requests it refused, kept for Wellness, **and the map key**, asked of Pool Maintenance once before its first visit to the screen and kept for the session. Nothing else."
→ The Watcher is now officially allowed to keep exactly one more thing: the key, for the session.

**12. Added lines ~210–211:** the Watcher now pulls in Pool Maintenance's description so it knows what a key is.

**13. Added lines ~411–415:** two new items in the Watcher's private memory: the key itself, and a yes/no marker for "have I already asked for it?" (starts as "no").

### Working file (+20 / −11)

**14. Added lines ~425–430:** at the start of a pool-map read, if the Watcher has not yet asked for the key this session, it asks Pool Maintenance, stores the key, and marks "asked". After that it never asks again in that session.

**15. Changed lines ~431–435:** where the Watcher used to open the pool map directly and take its slot count from it, it now takes the slot count from the key and the screen's starting point from the Live Registry. It still opens its read at the same moment as before.

**16. Changed lines ~436–437:** for each slot, it previously called the old routine (item 2). It now calls the new key-based routine (item 6), giving the reader's time stamp. Skip-if-empty behaviour is unchanged.

**17. Changed lines ~439–449:** each of the pool's details (id, class, turn, prompts, time stamp, size) used to be read as named parts of a pool. Each is now read at the distance the key gives, as the kind of value the key states. The list of prompts is copied out exactly as before.
→ Same seven details are gathered, same order, same result; only the way each is located differs.

**18. Changed lines ~450–451:** the note kept of where each slot is (so the Watcher can return to it) previously used the direct address of the slot; now it uses the address found through the key.

Everything after that in the read — checking the forbidden list against what was found and the rest of the loop — is untouched.

## Part 5 — Live Registry code file (its own latest change, `68ec0d4` vs the version before it)

This one is older news (27 Sep), included because the code file did not change in the latest commit.

**19. Changed comment lines ~127–131 (−3 / +1):** the note about clearing prompt links used to add "Pool-file tags are untouched: those are direct reads off standing pools, and their owner clears them when it tears those pools down." That sentence is gone; the note now only says the links are cleared every time this fires, win or lose.

**20. Removed lines ~278–319 (42 lines):** a whole section titled "Files — Pool ID → source file tag" is deleted. It was a table, written by the FileLoader when a pool was made, recording which source file each pool came from. It offered five actions: record a pool's file; look up a pool's file; list all pools from a given file (done by scanning, never a second table); forget one pool's file; forget every pool from one file.
→ The Live Registry no longer keeps any record of which file a pool came from, and none of those five actions exist any more.

Its description file was also edited at the same time (47 added / 30 removed) to match — that part reflects the same removal.

---

## What the change means, plainly

1. **Nothing a reader sees has changed.** Same pools, same moments in time, same older copies.
2. **Who knows the layout has changed.** Before, both Pool Maintenance and the Watcher knew it. Now only Pool Maintenance does; everyone else holds a key.
3. **One new duty for readers:** ask once per session, keep the key, use only the key.
4. **One new thing the Watcher may hold:** the key.
5. **Two design points for awareness (not blockers):**
   - The key is a session-long copy of directions, so if Pool Maintenance changed its layout mid-session a reader would still hold the old key. Nothing in the changed files says when a reader must ask again.
   - The Watcher still ends by handing back slot addresses (item 18) that it computed itself from the key, so it still treats the slots as real, reachable things.
