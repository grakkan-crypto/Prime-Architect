# KVBuilder

Brief spec. It records what has been settled about how each agent's context is built and read.

## What it is

KVBuilder is its own file. It turns pool content into KV cache for the models that read it, token by token as each pool is written. It is the mechanism by which an output is fed to an agent token by token and processed ready for generation.

## Sections

- **One section per pool per reading model.** Each section is built for one pool and one model that reads it. Agents on the same model share it. Different models have their own sections, because KV entries come from a model's weights.
- **Which models need a section** is resolved at pipeline load, from each agent's assigned models and the classes each agent can see.
- **Each section is computed from its pool alone.** It is never reused from the writing agent's own cache, which would carry traces of the writer's context.
- **Growth is at the end only.** An entry is written first; then the section's length is moved forward. A reader reads up to the published length and never sees half an entry. Readers keep no tail pointer of their own.
- **Pool content stays model-neutral** (JSON). Tokens exist only inside KVBuilder, per model's tokenizer. Tokenized forms are translations, not shadow copies. When agents move to Prime, every model shares one token vocabulary; the sections stay per model.
- **The codebase is not tokenized in advance.** Code blocks are ingested as their own sections when needed.

## How an agent reads

- **Pointers, not one block.** Each agent holds pointers to the sections of the classes it can see. Sections need not be contiguous; the kernel walks the list. Every step reads every section in the list up to its current length.
- **Masking and priority are one mechanism.** At scoring, before the softmax, a per-agent, per-class bias is added to every position of that class.
  - A positive bias gives the class more attention.
  - A negative bias gives it less.
  - Minus infinity gives it none: that is masking.
  - Workable bias ranges are calibrated per model.
- **Position is applied at read.** Each agent applies the positional rotation as it reads. No stored entry carries a position.
  - Moving a section recomputes nothing.
  - Section positions are tuned per class by measuring attention over turns, within the model's trained context length.
- **Layout.** Rules/Directives at the top, context in the middle, the prompt last. Authority comes from the Arbiters, not from position or markers.

## Agents

- **Agents are resident and never sleep.** A new prompt is ingested ahead of time, masked, into the same pool flip as its context. One flip unmasks both, and the agent starts on its next step with no further ingest.
- **The step rule:** a step runs only while the agent has incomplete work unmasked. The agent's own end token ends its generation. No pool is read to decide this.
- **Switching prompt ID and context:**
  - sections staying are untouched;
  - sections leaving drop from the agent's list;
  - masked sections already present are unmasked, with no ingest;
  - only genuinely new pools are ingested.
- **Routing.** An agent writes into a pool only by direction, or by a routing marker in its own output that the generation loop reads as the token is produced. Tokens go only to pools the agent has write rights on. In Prime the marker is one token ID for every model.

## Memory and destruction

- **Pool Maintenance owns the whole block.** It sets the same permission gate around every KV section as around the pool it represents. By default the gate allows everything through. As owner of the bytes, Pool Maintenance knows who holds access and which kernel runs (generation steps) are in flight on them.
- **Destruction inverts the gate:**
  1. No new step is given those bytes.
  2. Steps in flight finish on intact memory.
  3. The pool's sections are freed when the last in-flight step completes.

  The longest wait is one step, for that section only. Nothing else waits, and nothing pauses.
- **Destruction is immediate.** A pool in use is not destroyed, so a destroyed pool has no step reading it beyond one in flight.
- **Section boundaries are the pool boundaries.** Every entry is labelled with its pool ID; its class is read from the pool map. This gives ContextMatcher exact pool boundaries and what to prune, with no reconstruction.

## Open

- The bias values per class, per model, set by calibration.
- The routing marker's form.
