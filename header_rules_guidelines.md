# Header rules guidelines

How a file's header and its rules are written. Every step below applies to every header, every time it is written or edited.

## Order: code first, rules second

1. **The code is generated first.**
2. **Then the header is checked against the code** and updated to match if necessary.

Rules are never written ahead of the code, and code is never bent to fit a rule written in advance. When the new code would break an existing invariant, the change is flagged, not made.

## The header's structure

Every header has two blocks, in this order.

```
FUNCTION
  Owns <responsibility>. It is the only place that <exclusive thing>.
  <One or two sentences of loose context.>

OFFICIAL RULINGS — STRICT RULES. A change that would break one is wrong by
definition. It is raised with the user, never made.

INVARIANTS
  XX-1  MUST <x>, because <what breaks>. Not <the tempting alternative>.
  XX-2  MUST NOT <tempting edit>, because <what breaks>.
```

- **FUNCTION** is the only place for description. It says what the file owns and what it is the only place to do. "Owns X, and is the only place that does X" sets the file's boundary, so no rule is needed saying it must not do another file's job.
- **The preamble** is identical in every header, word for word.
- **INVARIANTS** holds only the rules that pass every test below.

## Where invariants come from

FUNCTION is derived from the code. Invariants are not. A rule derived from the code only restates the code.

Invariants come from:
- **intent:** design decisions, and why they were made;
- **failure:** what has broken, or what a maintainer would plausibly get wrong.

Before writing any invariant, ask: *"What would a maintainer plausibly get wrong here?"* If invariants are being written without that question being asked, they are coming from the code.

## The admission test

A candidate rule goes into a header only if **all three** are true:

1. **A well-meaning edit could plausibly violate it.** For example a tempting optimisation, retry, cache, shortcut or convenience.
2. **Nothing else would catch the violation.** Not the compiler, not a type, not a test, not another file refusing it at runtime.
3. **It cannot be derived from the file's FUNCTION.**

## Ownership

- **Each rule has exactly one owner:** the file where the violating edit would physically be made. For example, a rule about what a sender must tell a receiver is owned by the sender.
- **Other files reference a rule by its ID; they never restate it.** Restated rules drift apart.
- **A rule already covered by an enforced global rule is not restated in a header.** Examples: no silent fallback, no shadow copy.
- **A rule that binds many files with no single natural owner goes in the global rules,** with an ID, and is referenced from the headers it touches.

## Writing a rule

- Each rule carries a stable ID: the file's prefix and a number (CM-1, CM-2, …).
- Each rule is one capitalised statement of what MUST or MUST NOT be, followed by **because** and what breaks if it is violated.
- Where useful, it names the tempting alternative it rules out: "Not by creation time."
- A rule states a constraint. It never describes:
  - what the code does;
  - what another file does;
  - a list of the file's functions.
- No history, no amendment notes, and no in-progress language ("yet", "for now", "today", "so far", "when it exists", "not built").

## The final pass

For each rule, name the specific edit that would violate it. **If no specific edit can be named, the rule is deleted.**

## The count

A header should need **four to six rules at most.** More than that means one of two things:
- some rules are descriptions, and fail the tests above; or
- the file is doing more than one job.

Either is resolved before the header is finished.

## IDs are stable

- An ID is never renumbered and never reused.
- A rule is never quietly reworded. Changing an invariant is a change to the rules, flagged and made only with the user.
- A removed rule's ID is retired.

## The steps, in order

1. Generate the code.
2. Write FUNCTION from the code: what the file owns and what it is the only place to do.
3. Gather candidate invariants from intent and failure, asking what a maintainer would plausibly get wrong.
4. Apply the admission test to each candidate.
5. Remove any candidate owned by another file, or covered by a global rule. Reference it by ID instead, where needed.
6. Write each survivor as MUST / MUST NOT, because, and the tempting alternative.
7. Do the final pass: name the violating edit for each rule, and delete any that has none.
8. Check the count. Over six, find the descriptions or the second job.
9. Check the header against the code once more, and update it if necessary.
