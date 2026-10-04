# Pool Maintenance — what it is meant to be

Everything in one place. Section A is what the user has said directly. Section B is what was agreed in discussion. Section C was carried in earlier versions of the file and has not been removed by the user. Section D was removed by the user. Section E is still undecided.

---

## A. Stated by the user

- Pool Maintenance is an OS layer. It owns its domain outright and asks no one for permission within it.
- It owns the bytes of the map.
- It owns the bytes of the screen.
- The screen is a reflection of the map's bytes: the same bytes, shown a second time. It is not a copy, and it is not an address.
- Nothing redirects.
- Readers read the map only through the screen, and nothing else.
- No reader ever sees an edit in progress.
- The screen has a permission barrier, the same as every pool. From that barrier Pool Maintenance knows which readers are on the screen.
- Memory is asked only of the RAM Manager.
- At boot, Pool Maintenance claims a block from the RAM Manager. The RAM Manager sets the amounts, with Wellness checking. No figures are written in Pool Maintenance.
- Part of that block is Pool Maintenance's private RAM, for anything it needs that is not VRAM. The map lives there.
- Map pages come from that private RAM first. When it cannot supply a page, Pool Maintenance asks the RAM Manager for one.
- There is no maximum number of pools.
- No unit crosses a page boundary.
- Many units share a page. Rewriting a whole page for one edit is acceptable.
- A unit holds only what it needs. Pool Maintenance takes no fixed allowance per pool that would leave room for extra information.
- Immunity on the map is a single yes/no bit. The detail of who it is immune from is looked up elsewhere.
- The key lives on the screen, so nothing has to be kept in step and no new key is ever pushed out mid-run.
- A single-field change, such as the destruction flag, does not get a separately built entry.

## B. Agreed in discussion

### The map
- The map is made of pages. Each page holds as many whole units as fit in it, and any spare bytes at the end of a page stay unused.
- Every unit is the same size.
- Everything a reader reads sits inside the unit, with nothing pointing outside it.
- The map is the pools. With no pool standing the screen shows nothing, and that is correct.

### The screen
- The screen is held on LiveRegistry, as a stretch of address space of its own.
- Each page of the screen shows one page of the map's own bytes, read-only.
- The screen opens with the key: unit size, units per page, and where each field sits. The unit count follows, then the units.
- Readers read everything off the screen and never ask Pool Maintenance anything.

### Edits
- **Changing a unit:**
  1. Pool Maintenance builds a new page in its own memory, holding the changed unit and its neighbours as they are.
  2. In one step, the screen switches that page to show the new page.
  3. The old page is freed once the screen's barrier holds no reader that arrived before the switch.
- **A single field** (for example, the destruction flag) is written in place, in one step that cannot be split.
- **The map growing:**
  1. A new page is taken.
  2. The screen's next page is set to show it.
  3. The unit count on the screen is updated in one step.
- **Destroy on the map:** the flag is written in place. The unit later shows as empty. How empty is marked is still open (see E).

## C. Carried in earlier versions of the file, not removed

### Standing
- Pool Maintenance takes instruction from the rest of the system. Whether a pool exists or is destroyed arrives as an instruction.
- **Its authorities:**
  - pool-level access over every pool and every model's KV cache;
  - who holds the pool map screen on LiveRegistry;
  - which bytes go back when RAM is reclaimed;
  - when an instructed destruction is safe, and carrying it out then.
- **Pool Maintenance and the RAM Manager are co-dependent.** The RAM Manager supplies the RAM, and Pool Maintenance decides how it is used and what is returned. Neither sits above the other.
- **It is mechanical.** It never decides whether a pool should be destroyed, flagged or kept.

### Memory
- Pool memory held is the VRAM part of the boot block plus every stretch received since. Free memory is what is held, less what pools are using.
- When free memory cannot cover a spawn, the shortfall is asked of the RAM Manager and the spawn goes ahead.
- When the RAM Manager asks for memory back, Pool Maintenance chooses which bytes go. It never gives up bytes in use, or its private RAM.
- Only the boot block has to be continuous.
- Each new pool takes its own 1 MiB of the VRAM part.

### Pools
- **A pool is bytes, never tokens.**
- **A pool has no name.** Its Pool ID is its only identity.
- **The stamp:**
  - It holds Pool ID (minted by IdGeneration), Class ID, Turn ID, Prompt ID(s) and timestamp.
  - It is set at creation and never changes. Class ID is the only exception, and only through Reclassify.
  - Turn ID and Prompt ID(s) may be empty.
  - Prompt ID(s) are copied off the one pool this pool continues.
- **Class ID** is read when it is needed and never held. A name is resolved through LiveRegistry at that moment.
- **Create:**
  - The caller fires and moves on. Nothing is handed back, except the Pool ID when the caller asks for it.
  - Each create is one pool. A refusal touches that pool alone.
  - A create is refused when the class resolves to nothing, when the pool it continues is missing or flagged for destruction, or when there is no room.
- **Grow:** whatever is writing decides when to grow, by reading capacity off the entry. Pool Maintenance never watches for it.
- **Destroy, flag and unflag** use one filter: Class ID, Turn ID, Prompt ID and Pool ID, all required to match, with exclusions.
- **Immunity** protects a pool from exactly the source named. Destroy checks it itself.
- **Reclassify** changes Class ID and nothing else.
- **A pool's bytes are gated:**
  - A reader arrives on the pool and leaves it.
  - A pool flagged for destruction admits no new readers.
  - When the last reader leaves, the pool ends.
  - The system leaves on behalf of a reader that died.
- **The same barrier** stands around every KV section that represents a pool.

## D. Removed by the user

- The redirect, preserved copies and static images.
- Readers holding an object, announcing themselves or counting themselves in order to read.
- The screen as the map's address.
- Any copy of the map.
- Every Wellness flag.
- Every chunk request, and every mention of chunks.
- Any fixed maximum number of pools.
- The 80 GiB and 1 GiB figures.
- The file's header.

## E. Undecided

1. Which fields a unit holds, how long each ID is, and the most prompt IDs one pool can carry.
2. How an empty unit is marked (a separate field, or a blank Pool ID), and whether such a marker exists at all.
3. Whether emptied units are reused.
4. Where the immunity detail lives.
5. How grow obtains memory.
6. Whether a destroyed pool's 1 MiB is given back.
7. How Wellness is told of anything.
8. The file's header. CLAUDE.md requires every file to carry rules in its header, and this file's header has been removed.
