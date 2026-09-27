# Pool Maintenance — Build Spec

Sep 27, 2026 · @Lee

The edits confirmed for Pool Maintenance, the screen on LiveRegistry, and Watcher. Where this spec disagrees with the user, the user is right.

## Out of scope

- **IDs.** Where the pool, turn and prompt IDs come from, and how turn and prompt IDs follow continuation. Pool Maintenance mints no ID. The source is its own spec.
- **Masking.** The destroy mark requires the marked pool to be masked from every model. How Masking does that, and its move to the three commands (flip, mask on, unmask), is its own spec.

## 1. What Pool Maintenance is

- **Part of PrimeOS.** It lives in the PrimeOS folder, which FileLoader keeps physically write-protected. A change to Pool Maintenance is a user update. A model can draft one but cannot make it.
- **One function: sole ownership of pools.** A pool comes into existence here and leaves existence here, nowhere else. Nothing else creates, changes, locks or destroys a pool or its bytes.
- **Sovereign.** It asks no one's permission for anything to do with pools. There is no separate OS. Any low-level control it needs, it holds and uses directly.
- **Mechanical.** It never decides whether a pool should exist, be kept or be destroyed. It carries out what a caller holding that authority asks.
- **One caller at a time.** Each action runs to completion before the next caller is served. There is no batching.

## 2. Memory

- **One standing allocation.** Pool Maintenance holds a large block of memory from system start (around 80GB of 128GB). Pools are carved from it freely, and sit together instead of scattered across RAM among unrelated things.
- **The map lives in Pool Maintenance's own reserved memory.** That memory never needs a request.
- **RAMManager**, which is PrimeOS Core, does the high-level accounting of RAM.
  - Pool Maintenance asks RAMManager for more only when its allocation runs short.
  - When another system needs memory, RAMManager asks Pool Maintenance, and Pool Maintenance gives some back.
  - RAMManager has nothing to do with individual pools.
- **Needing more is the alarm.** The moment Pool Maintenance needs to ask RAMManager for more, Wellness is told. Pools past their allocation must be dealt with long before anything runs out.

## 3. The pool

- **Bytes, never tokens.** No size, capacity or content is ever expressed in tokens.
- **No name.** The pool ID is its only identity. Nothing is keyed by a readable label.
- **Contiguous.** Each pool is one unbroken stretch of address space, reserved in full at create. Physical memory is attached in hardware pages as the pool fills.
  - The bytes never move and never hop between pieces.
  - There are no chunks and no chunk list.
- **Automatic growth.** When the writer reaches the end of the memory attached so far, still inside the pool's own reserved stretch, Pool Maintenance attaches the next page on the spot. No one calls grow.
- **The stamp is the pool's metadata:** pool ID, class ID, turn ID, prompt IDs, creation time.
  - Everything except the class is fixed at mint. Reclassify alone changes the class.
  - The stamp carries one editable field, the **progress count**: how many bytes of content have been written. Token generation writes it, and readers never read past it.
- **Everything else on the entry:**
  - the start of the pool's stretch and how many pages are attached
  - the immunity list
  - the destroy mark, when set

## 4. The map and the screen

- **The map belongs to Pool Maintenance.** No reader touches the map itself. It is read only through the screen on LiveRegistry.
- **The screen is a live view of the map, not a published copy.**
  - Editing the map is the action. There is no publish step, and nothing to keep in sync.
  - A freeze cannot knock anything out of step, because nothing is copied.
- **The image exists only while an edit is being made.**
  - Before an edit changes any block of the map, that block's original contents are kept aside. Anyone already reading keeps seeing the unchanged bytes.
  - Only changed blocks are kept. They are never synced, and Pool Maintenance frees them when the last holder of that image leaves.
- **Pool Maintenance never waits on readers.** No one sees half an edit.
- **Pool Maintenance sees every reader of the map directly.** Reaching the view is itself visible to it.
- **Entries sit where they are.** Nothing is grouped or reordered by class, turn or prompt. Class, turn and prompt are on each entry, and Pool Maintenance cross-references them itself when a request names them.
- **The pool map hub**, owned by Pool Maintenance, states the image's name and layout. Every reader reads the hub first.

## 5. Each pool is a protected space

- **Granted or refused.** Pool Maintenance either grants a pool unrestricted access or refuses it completely.
- **Open pool.** A file reaching it automatically gets a link.
- **Locked pool.** It refuses every new link.
- **Counted.** Pool Maintenance knows exactly how many links each pool has, because it gave every one.

## 6. Access

- **Files** read every pool. They are not agents, so there is no context to protect.
- **Files write into a pool only at mint.** Rules and ProjectIngest do this. After mint, the only writer is the agent generating into the pool. No file ever edits an agent's output.
- **Models** take access from the pipeline's permission table, by the pool's class:
  - being listed gives read access
  - the write bit and the annotate bit give the rest
- **Reclassify** changes the class, and access follows the new class through the table by itself.
- **Rules pools** hold no reader list. An agent can see a Rules pool or it cannot.
- **Watcher** reaches everything it needs.

## 7. The actions

### Create

1. The caller gives the class, as its number or its declared name. A name is resolved by reading LiveRegistry at that moment.
2. Pool Maintenance obtains the pool ID and stamps the pool. The ID sources are out of scope.
3. It reserves the pool's stretch and attaches the first page.
4. A file creating the pool may write its initial content now, at mint only.
5. It places the entry. That is the moment the pool exists.

The caller is handed nothing back. Whether the pool came to stand goes to Wellness. A refusal leaves nothing standing, and happens when the class does not resolve or there is no memory to give.

### Destroy

1. The caller gives any combination of pool ID, class, turn and prompt, with exclusions, plus the source asking. Only what is given is checked; a pool ID alone is one pool, a class alone is the whole class. Pool Maintenance cross-references the map itself.
2. **Immunity is checked at the point of destroy.** A pool immune to the source asking is not touched.
3. Every other match gets the **destroy mark**, visible on the map. It tells every reader the pool is eligible for imminent destruction and not to enter. From that instant:
   - the pool refuses new links
   - writing to it is closed
   - it is masked from every model (the Masking side is out of scope)
4. Links already out are given up as their holders finish.
5. **When a marked pool's link count reaches zero, Pool Maintenance destroys it immediately.** It removes the entry and returns the memory to its own allocation.

This one flow covers every pool, system pools included. A Rules pool can stay marked for as long as its holders need without disturbing the running system. A holder that never lets go is Wellness's to judge.

### Flag / unflag

A source name is added to or removed from each matching pool's immunity list. The match uses the same filter as destroy. A flag protects against exactly the source named and no other. Destroy checks immunity itself; no caller ever does.

### Reclassify

The class ID on the entry changes, and nothing else.

## 8. Wellness

Each posting is a plain true or false, named for the question it answers, set the instant it is answered and never read again by Pool Maintenance.

- Did the pool come to stand? (every create)
- Did the class resolve? (create, reclassify)
- Did Pool Maintenance need more memory from RAMManager? (the instant it does)
- Did RAMManager have it to give? (the instant it answers)

A refusal is never silent. Nothing is ever reverted to tidy up, and no stand-in value is ever used in place of a missing one.

## 9. Removed from Pool Maintenance

- every call to "the OS": the future layer, the mount, and chunks taken from and returned to the OS
- the publish or mount step, and the "edited but not mounted" outcome
- chunks, the chunk list and chunk size
- grow as a caller action
- shrink
- minting any ID
- the continuation input, and copying prompt IDs from the pool continued
- handing the pool ID back to the caller
- locking driven by Watcher's forbidden list
- the "gone" read and its report
- batching several actions under one publish
- "create unless a matching pool exists"
- grouping entries by class
- the reader count that edits wait on

## 10. Pool Maintenance header rules

Replaces the current rules section in full. Preamble and format are as the project instructions set them.

> OFFICIAL RULINGS — STRICT RULES. A change that would break one is wrong by definition. It is raised with the user, never made.

1. **POOL MAINTENANCE IS THE SOLE OWNER OF POOLS.** Nothing else creates, changes, locks or destroys a pool or its bytes, and Pool Maintenance asks no one's permission to do so.
2. **THIS FILE CHANGES ONLY BY USER UPDATE.** It sits in the protected PrimeOS folder. No model edits it.
3. **A POOL IS BYTES. NEVER TOKENS.** No size, capacity or content is ever expressed in tokens.
4. **A POOL HAS NO NAME. POOL ID IS THE ONLY IDENTITY.**
5. **THIS FILE MINTS NO ID.**
6. **CLASS ID IS READ, NEVER HELD, NEVER WRITTEN BACK.** A name is resolved by a direct read at the moment it is needed, every time.
7. **THE STAMP IS FIXED AT MINT.** Reclassify alone changes the class, and nothing else. The progress count is the one field written after mint, and only token generation writes it.
8. **THE MAP IS POOL MAINTENANCE'S. NO READER TOUCHES IT.** It is read only through the screen. This file offers no lookup, find or listing of pools.
9. **THE SCREEN IS A LIVE VIEW, NEVER A PUBLISHED COPY.** An image exists only while an edit is made, from the blocks it changes kept aside. Nothing is synced.
10. **POOL MAINTENANCE NEVER WAITS ON READERS.**
11. **POOL MEMORY IS ONE STANDING ALLOCATION.** Pools are contiguous and grow by attached pages. Needing more from RAMManager is itself a Wellness posting.
12. **FILES WRITE INTO A POOL ONLY AT MINT.** After mint, only the generating agent writes.
13. **ONE FILTER, NO VARIANTS.** Destroy, flag and unflag share it. A new combination a caller needs is expressed through it, never given its own function.
14. **IMMUNITY IS SCOPED, NEVER BLANKET.** A flag protects against exactly the source named. Destroy checks it at the point of destroy; no caller checks it.
15. **A MARKED POOL IS DESTROYED WHEN ITS LAST LINK IS GONE, NEVER BEFORE.**
16. **THIS FILE IS MECHANICAL.** It never decides whether a pool should exist, be kept or be destroyed.
17. **NO REFUSAL IS SILENT. NOTHING IS REVERTED.** Wellness is told the true outcome, and no stand-in value is ever used.

## 11. LiveRegistry: the screen

The screen's build outline is replaced to match section 4:

- **Live view.** The screen is a live view of the pool map, written only by Pool Maintenance.
- **The image exists only during an edit.** It is built from the changed blocks kept aside, and Pool Maintenance frees it when its last holder leaves.
- **Pool Maintenance sees every reader directly, and never waits on them.**
- **No "system core" or OS.** Neither keeps blocks aside, records holders or writes the screen. All of that is Pool Maintenance's.
- **Removed:** "latest complete version", and Pool Maintenance "putting the new version on the screen".

Nothing else on LiveRegistry changes.

## 12. Watcher

- **The map read** is through the screen, as now.
- **The content read** runs from the start of the pool up to its progress count, and never past it.
- **The forbidden list stays and keeps its name.** It becomes Watcher's own skip list: pools whose stop token it has read, which will not change again, so it stops reading them.
  - Nothing else reads the list and nothing is locked by it.
  - The published copy of it is removed.
  - An ID leaves the list when it leaves the map.
- **Watcher's pool record** carries capacity as the bytes of attached pages.

## 13. Documents to correct

- **Core-layer permissions spec.** Remove the "system core" block-keeping and holder tracking; both are Pool Maintenance's. Remove "puts the new version on the screen". Remove Pool Maintenance reading Watcher's forbidden list, and the forbidden-list locking. The lockout is the destroy mark, as in section 7.
- **Pool Map Viewing Screen build spec.** Superseded by this spec. Its reader count, edit hold, OS stubs, batching, match-before-mint and forbidden-list lock do not apply.
