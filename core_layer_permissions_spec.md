# Core-layer permissions: Pool Maintenance and FileLoader

Loose spec. It describes what each layer controls and where its control ends. It does not describe how either is built.

## The system model

PrimeOS is an AI server, all-in-one system. There is no application running on top of an OS. Each core layer owns its domain outright and controls it directly: it asks nobody for permission within that domain, because nobody sits above it. It asks another layer only for what belongs to that other layer's domain.

| Layer | Domain |
|---|---|
| Pool Maintenance | Pools and the pool map |
| FileLoader | The disk: every load, save and delete |
| Memory distributor | RAM and VRAM: handing out and taking back memory |
| Wellness | System health: live patching, debugging, alignment |
| System core | What spans every layer at once (see below) |

## Pool Maintenance: the core layer for pools

**Sovereignty over pool bytes.** While bytes are a pool, Pool Maintenance has total control over them: who may read, who may write, and when they are destroyed. When the bytes are handed back to the memory distributor, that control ends completely. Nothing needs unlocking on the way out; the bytes simply belong to the distributor again.

**Memory.**
- A section of memory is reserved to Pool Maintenance at all times for its own work, including the pool map. That never needs a request.
- The one request it makes is to the memory distributor, when it needs more memory for pools.

**The pool map.**
- The map is Pool Maintenance's, and no reader ever has access to the map itself.
- Pool Maintenance edits the map directly and never waits on readers.
- When an edit is complete, it puts the new version on the screen itself.
- Only Pool Maintenance and the system core can write the screen. The screen sits in memory of its own, read-only to everything else.
- The pool map hub states the image's name and layout. Pool Maintenance owns the hub, and every user of the pool map reads the hub first.

**Forbidden pools.**
- Pool Maintenance reads Watcher's published forbidden list directly.
- A forbidden pool is locked against every file straight away. Models keep read access, because they generate from those pools.
- Immediately before destruction, models are locked out too, and then the pool is destroyed and its bytes handed back.

## FileLoader: the core layer for the disk

**The one gate.** FileLoader is the only place anything is loaded from, saved to, or deleted from disk. It is not asking anyone for access and not reporting to anyone: it is the layer that controls the disk.

**Access is by need.**
- A part of the system that needs to read something is let through. For example, Watcher reads the files named in its requests directly, and Archivist has the door held open to the RAG.
- Anything else is blocked at the point of attempt.

**No ID mechanism.** Because FileLoader is the gate, it does not need to tag or track pools and files by ID to control access.

**Delete.** Deleting files belongs to FileLoader's domain.

## What stays with the system core

These cover every layer at once, so no single domain owner can hold them:

- **Keeping blocks aside.** Before a write changes a block that an older version of the pool map still holds, the block's contents are kept aside for that version. The hardware triggers this, below every layer.
- **Tracking holders.** The core knows which reader holds which version of the pool map, across the whole system, and frees a version when its last holder drops it.
- **Enforcing each layer's memory protection.** The hardware stops any write to memory that a layer has made read-only.

## Wellness

- **During a code edit,** Wellness reads the code being generated and checks it against the live system for anything that needs informing.
- **When code is updated,** it goes straight to each affected part with the new spec, as if that part had just been given it. It forces a reload where one is needed.
- **Pausing.** It may pause the whole system to bring everything into line, for example before a change to the pool map's name or layout reaches the screen.
- **Failures.** Dead or stuck readers, an empty screen, and any other failure are Wellness's to judge and act on.

## Awareness: not yet aligned

- FileLoader as it stands in the repo is not the disk gate. It becomes the gate by a rebuild, not an edit.
- The memory distributor as it stands in the repo is the old allocator, which reserved one large block for itself and shared it out. It becomes the system-wide distributor for RAM and VRAM by a rebuild. On this unified-memory machine, RAM and VRAM are the same physical memory. Each byte carries a RAM or VRAM designation that decides where it is placed and how it is physically optimised, and both sides can read both.
- Pool Maintenance as it stands in the repo keeps its pools as private bookkeeping and publishes no pool map hub. Watcher refers to the image by name and layout from the hub, so Watcher will not build until Pool Maintenance publishes it.
