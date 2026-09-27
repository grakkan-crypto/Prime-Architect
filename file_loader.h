// file_loader.h — THE FILESYSTEM GATEKEEPER
//
// Loader is the filesystem gatekeeper: every disk read and write in this
// system happens here, verified against who is asking. Those two things —
// the disk mechanics and the identity they are checked against — are one
// purpose, not a function plus an addition to it.
//
// THE WORD "GATEKEEPER" IS BOUNDED, DELIBERATELY.
//   It means exactly this: identity-checked disk access. It is not a
//   licence to move unrelated function into this file because the file
//   "guards things". Anything that is not a disk read, a disk write, or
//   the identity those are checked against does not belong here, and
//   citing the word "gatekeeper" is not an argument that it does.
//
// ===========================================================================
// OFFICIAL RULINGS — FIXED POINTS. READ BEFORE TOUCHING THIS FILE OR
// BUILDING ANYTHING AGAINST IT.
//
// These rulings are not conventions, preferences, or defaults that a good
// enough reason can override. A change that would break one of them is
// wrong by definition and is raised with the user instead of made.
//
// 1. LOADER HAS EXACTLY THREE FUNCTIONS OF ITS OWN: LOAD, SAVE, AND FILE
//    SECURITY. No fourth function is ever added to this file — not under a
//    new name, not as a helper, not as a wrapper, not "temporarily". If a
//    task appears to need one, the task is misassigned: the missing
//    behaviour belongs in the content a Layer hands over, never in this
//    file. Load and save are SIBLINGS — each is its own door, reached
//    directly. A Layer that only needs to write something already fully
//    formed calls save on its own; it never fakes a load to get there.
//
// 2. LOADER IS THE SOLE ROUTE FOR READING, PROCESSING, AND STORING.
//    Every file read from disk, anywhere in this system, happens inside a
//    Loader call. Every piece of processing applied to what was read runs
//    inside a Loader call. Every store of a result — wherever that result
//    lands — happens inside a Loader call. No other file reads disk. There
//    are no exceptions and no case small enough to be exempt.
//
// 3. LOADER IS THE SOLE AUTHORITY FOR WRITING TO DISK.
//    Nothing else in this system ever writes a file. A disk write found
//    anywhere outside this file is a defect, not a design choice.
//
// 4. A LAYER FILE PROCESSES NOTHING. EVER.
//    The Layer is the function; Loader is the functionality. A Layer hands
//    Loader content that is already fully formed — every step, every
//    decision, every destination written into it — and Loader executes all
//    of it. If code on a Layer's own stack is reading, parsing, deciding,
//    or transforming anything, that code is in the wrong place and moves
//    into the content handed to Loader.
//
// 5. LOADER HOLDS NOTHING.
//    Construction takes no arguments. No member state exists. Nothing
//    survives from one docking to the next, and nothing is ever added that
//    does. Loader knows nothing before a docking, keeps nothing it learns
//    during one, and holds nothing after it. The Layer ID is no exception:
//    it is minted for a docking, used for that docking's whole duration,
//    and gone with it.
//
// 6. LOADER CARRIES NO LAYER VOCABULARY.
//    No concept of a pool, a class, an agent, a pipeline, a mask, or
//    anything else named after what a Layer does with its data appears in
//    this file — not in code, not in comments. Such a word appearing here
//    is itself the proof that something has drifted into the wrong file.
//
// 7. LOADER IMPOSES NO ORDER.
//    Handed-over content specifies its own sequence — read, decide, write,
//    read again, in whatever order and however many times it states.
//    Loader never assumes a read comes first, a write comes last, or that
//    any step happens exactly once. Entries in one request run in
//    parallel; sequence exists only inside an entry's own content, where
//    one step's input genuinely is another step's output.
//
// 8. LOADER NEVER GROWS TO SOLVE A LAYER'S PROBLEM.
//    No capability is ever added here because one Layer needs it. A need
//    unique to any Layer — or common to several — is authored in those
//    Layers' content. This file changes only if the mechanics of reading
//    or writing disk themselves must change.
// ===========================================================================
//
// THE DOCKING — WHAT ONE CALL IS
//
//   A docking is one top-level entry into Loader: one call to load, or one
//   call to save. It is scoped to the WHOLE handed-over function, not to
//   any single disk touch inside it. Content that resolves many paths,
//   pulls many files, transforms them, and writes many results is still
//   ONE docking — one call, one identity, from the moment it starts to the
//   moment every step it specified has finished. The docking ends when the
//   call ends — completion, failure, or throw, it does not matter which.
//   The call's own scope ending IS the undock; nothing outside this file
//   announces it, because nothing has to.
//
// IDENTITY — RETRIEVED, NEVER OFFERED
//
//   A Layer file has NO idea what a Layer ID is. It does not hold one,
//   does not ask for one, does not pass one, does not make room for one in
//   the content it authors, and cannot see one. There is no action on a
//   Layer's side to imitate, because supplying identity is not something a
//   Layer does.
//
//   Loader RETRIEVES the Layer's identity — it is never offered up by the
//   Layer. A Layer stating its own identity would be an assertion, and an
//   assertion is exactly what can be forged. Loader HAS the docking
//   source's full canonical path — how it arrives in Loader's hands is out
//   of this file's scope — and mints a fresh identity for the docking. The
//   row it logs, path beside identity, is the ONE record of whose identity
//   this is, and it dies with the docking. The value alone proves nothing,
//   so there is no credential to copy, and no second copy of the pairing
//   exists anywhere to drift, leak, or grow.
//
//   THE PATH IS THE FILENAME. Every file in this system is named by its
//   entire canonical path — see the system-wide naming requirement. There
//   is no runtime query to intercept and no value handed over to falsify,
//   because a file's origin is carried in what it is called.
//
//   THE ID IS PER DOCKING, NOT PER OPERATION AND NOT PER LOADER. One
//   identity is minted at the start of a docking and every read and every
//   write that docking's content performs — however many, in whatever
//   order — carries that same one identity, because they are all inside
//   the one call that minted it. Not because anything was stored and
//   fetched back for them: nothing ever is.
//
//   FRESH AND RANDOM, EVERY TIME. Every docking mints a NEW identity.
//   An identity is never looked up, never pulled back out of storage,
//   never carried forward from a previous call, never reused. The moment
//   an identity could be retrieved and reused it becomes a value that sits
//   still long enough to be learned — and a learned credential is a
//   spoofable one. Random every time and checked every time is what makes
//   the check unforgeable.
//
//   ONE CHECK AT THE DOOR. The identity exists so the request can be
//   verified as coming from a genuine source and not a backdoor mechanic.
//   That verification happens once, at the gate, for the docking. If it
//   passes once, everything the docking's content does rides under it —
//   there is nothing left to re-check step by step inside, because the
//   whole thing only got in through the one gate.
//
//   LOADER VERIFIES NOTHING ITSELF. It does not know what an identity
//   authorises and never will — the same way it never knows what any of
//   the content it executes is for. It retrieves identity, attaches it,
//   logs it, and lets it go. The check belongs to the OS and does not
//   exist yet.
//
// THE IDENTITY TABLE — LOADER'S ONE NARROW REACH INTO THE LIVE REGISTRY
//
//   For every docking, Loader logs ONE ROW into one narrow table held by
//   the live registry: the docking Layer's full canonical path, and the
//   identity minted for this docking. The row goes in when the identity is
//   minted and comes out when the docking ends — however it ends. That is
//   the row's entire life.
//
//   WHAT THE TABLE IS FOR. The OS-level filesystem check (future work,
//   not built, not approximated here) reads this table to verify that a
//   request touching disk traces to a genuinely docked source — has it any
//   access at all, and has it access to what it is touching. Loader only
//   keeps the table true; it never reads it back and never consults it.
//
//   SOLE WRITER, SOLE ERASER. Loader writes this table and Loader clears
//   it, row by row, each docking cleaning up its own. Nothing else touches
//   it in either direction. Because the one writer removes what it adds,
//   the table cannot grow without bound — it holds exactly the dockings
//   live at this moment and nothing else. Several Layers docked at once is
//   ordinary: each docking has its own row under its own identity, and
//   each row dies with its own docking.
//
//   NOTHING ELSE OF THE REGISTRY IS VISIBLE HERE. Not its sections, not
//   its questions, not its vocabulary. The boundary below is the entirety
//   of Loader's reach: write one row, erase one row. This file does not
//   include the registry's own header, deliberately — what cannot be seen
//   cannot be leaned on.
//
// WHAT THIS FILE IS
//
//   Loader knows nothing and does everything. A Layer knows everything and
//   does nothing. Load and save are the carved-out capabilities that are
//   Loader's own; every other behaviour that runs here arrives as content,
//   fully formed, and is executed exactly as written.
//
// THE PORT
//   A Layer file docks, hands over its function, and undocks when that
//   function has run. Loader executes all of it. It holds nothing before
//   the docking and nothing after it — no function retained, no caller
//   remembered, no state between dockings. When the Layer withdraws,
//   whatever it knew goes with it; Loader never held any of it.
//
// THE REQUEST
//   A load request is a list of entries. An entry is one self-contained
//   piece of Layer-authored content: a function carrying every step, every
//   decision, every destination, and every reference it needs, bound in at
//   the moment the Layer built it. Loader executes the entry, handing it
//   the two disk mechanics — read and save — so that every disk touch the
//   content directs still happens by Loader's own hands.
//
//   A save is the direct write door: one path, one piece of already fully
//   formed content, written by Loader's hands under this docking's
//   identity. Nothing is executed, because the Layer's decisions are
//   already over — the content arrives finished.
//
// WHAT COMES BACK
//   Loader states only what Loader itself knows: whether each entry ran to
//   completion, and the stated reason where one did not. Anything a Layer
//   wants back from its own content travels through what that content was
//   built holding — never through this file.
//
// THREE ATTEMPTS, THEN LOUD
//   A disk read that fails is tried three times before it counts as a real
//   failure. Absent is not retried — absent is a state, not a fault.
//
// ABSENT IS NOT UNREADABLE
//   A file that is not there yet is an ordinary state — what everything
//   looks like before anyone has saved anything. A file that IS there and
//   cannot be read is a real failure. Reporting both the same way would
//   let unreadable content silently present as "nothing saved yet".
//
// PARTIAL LOADS ARE REPORTED, NEVER SILENT
//   Entries run independently. A failing entry is recorded and the
//   remaining entries still run — the report says precisely which entries
//   completed and which did not and why.
//
// EVERY REFUSAL IS LOUD
//   Every failure reason is built at the point of failure from what is
//   already there. Nothing is carried around in advance for a failure that
//   will almost never happen.
//
// THE STAGED WRITE
//   Every write goes to a temporary sibling and is renamed over the
//   target, so a crash part-way through cannot leave a half-written file
//   that would then load as real, truncated content.
//
// FAILURE ROUTING — FORWARD NOTE
//   A system-wide error log (specced separately, not yet built) will
//   receive every failure this file produces. The marked points in
//   file_loader.cpp are where that hand-off will land; nothing about that
//   system is approximated here in the meantime.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace prime {

// The three states of a read. Absent and unreadable are DIFFERENT, always: a
// file that is not there yet is an ordinary state — what everything looks
// like before anyone has saved anything. A file that IS there and cannot be
// read is a real failure. Returning empty for both would let unreadable
// content silently present as "nothing saved yet".
enum class FileRead {
    Ok,
    Absent,      // not there yet — ordinary
    Unreadable,  // there, and could not be read — a real failure
};

// ===========================================================================
// *** PLACEHOLDER — THE OTHER HALF DOES NOT EXIST YET ***
//
// LayerId is the identity minted for a docking. Loader's entire relationship
// with it is: mint it, log it, attach it, erase its row, discard it. Loader
// never inspects it, never interprets it, and never checks it against
// anything — the permission check that gives it meaning is OS-level work
// that does not exist yet.
//
// Until that exists, this does NOT stop a Layer accessing a file it should
// not — nothing can, until there is a permission table to check against.
// Do not treat the presence of this type as evidence that access is being
// controlled today.
//
// If this shape fits the eventual permission system, keep it. If it does
// not, replace it. Nothing about this type is a settled design choice, and
// "Loader uses this shape" is not authority for what the permission system
// must look like.
// ===========================================================================
using LayerId = std::string;

// ===========================================================================
// THE IDENTITY TABLE BOUNDARY — Loader's ONLY reach into the live registry.
//
// Two motions, and only two: put one row in, take one row out. The row is
// (the docking source's full canonical path, the identity minted for this
// docking). Implemented on the registry's side; declared here so this file
// sees exactly this much of the registry and not one thing more.
//
// These are LOGGING motions, nothing else. Loader enforces nothing on the
// back of them — verification against this table is the OS filesystem's
// job and does not exist yet. Failures on the registry's side go to the
// error log system once it exists.
//
// The erase is keyed by identity, because identity is unique per docking —
// the same source docked twice concurrently is two rows, and each erase
// takes exactly its own.
// ===========================================================================
void identity_table_dock(const std::string& layer_path, const LayerId& id);
void identity_table_undock(const LayerId& id);

// ===========================================================================
// THE PATH — LOADER HAS IT. How it arrives in Loader's hands is explicitly
// out of this file's scope and is not built, approximated, or guarded here.
// This declaration is the single point it arrives through; the mechanism
// behind it lands elsewhere. No behaviour of any kind hangs off it in this
// file — Loader has the path and uses it, and that is the whole story.
// ===========================================================================
std::string docked_layer_path();

// ---------------------------------------------------------------------------
// The disk mechanics, as handed to executing content. These are Loader's
// own capabilities — the same read and save, made callable from inside an
// entry's content so that every disk touch the content directs still
// happens by Loader's hands. Only Loader constructs this; no code reaches
// disk without being inside a docking that minted an identity for it.
// ---------------------------------------------------------------------------
class Disk {
public:
    // Three attempts, then the outcome stands. Absent is never retried.
    // Carries this docking's identity, attached by Loader.
    FileRead read(const std::string& path, std::string& out) const;

    // The staged atomic write, under this docking's identity, attached by
    // Loader. Content supplies nothing and sees nothing.
    bool save(const std::string& path, const std::string& text) const;

private:
    explicit Disk(const LayerId& id) : id_(id) {}
    LayerId id_;
    friend class FileLoader;
};

// What one entry's content states about its own run. The failure wording
// is built by the content, at the point of failure, from what it already
// holds — this file adds nothing to it.
struct EntryOutcome {
    bool        ok = false;
    std::string failure;   // populated only when ok == false
};

// One entry: one self-contained piece of Layer-authored content. Handed
// the disk mechanics and nothing else; everything further it needs was
// bound into it when the Layer built it. A request carrying no entries at
// all is a stated failure — a Layer is fully formed and plug-and-play; it
// always has content.
using EntryFn = std::function<EntryOutcome(const Disk&)>;

struct LoadEntry {
    EntryFn run;
};

struct LoadRequest {
    std::vector<LoadEntry> entries;
};

// What Loader itself knows about one entry's run.
struct EntryReport {
    bool        ok = false;
    std::string failure;
};

struct LoaderReport {
    bool                     ok = false;  // every entry completed
    std::string              failure;     // the first failing entry's reason
    std::vector<EntryReport> entries;     // one per request entry, in order
};

// What Loader itself knows about one save docking's run.
struct SaveReport {
    bool        ok = false;
    std::string failure;   // populated only when ok == false
};

class FileLoader {
public:
    FileLoader() = default;

    FileLoader(const FileLoader&)            = delete;
    FileLoader& operator=(const FileLoader&) = delete;

    // THE LOAD DOOR — one docking. Loader mints this docking's identity,
    // logs its row, attaches the identity to the disposable content it was
    // handed, and executes that content — every entry in parallel. Entries
    // are independent; what happened to each is stated per entry. The
    // docking ends when this call ends — however it ends — and the row and
    // the identity end with it. Whatever this returns goes to the exact
    // stack frame that called it, once, and is held nowhere inside this
    // file.
    LoaderReport load(const LoadRequest& request);

    // THE SAVE DOOR — one docking, standing on its own beside load. For a
    // Layer whose content is already fully formed and only needs writing:
    // no content is executed, nothing is faked to get here. The same
    // minting, the same row, the same one identity for the docking's
    // duration, the same staged write by Loader's own hands, the same
    // erase when the call ends.
    SaveReport save(const std::string& path, const std::string& text);
};

// ---------------------------------------------------------------------------
// Scanning the structured text — the Loader's generic toolkit.
//
// ONE implementation of the shape this system writes everywhere it writes
// text: quoted names, values after a colon, braces grouping a record,
// brackets grouping a list. Every configuration that parses that shape
// (Rules, Directives) calls THESE — nothing keeps its own copy, because a
// second copy of the same scanning is a second thing that can disagree
// about what a file means. Deliberately literal: they find what is named
// and report when it is not there, and they do not try to be a general
// parser — the files are small and this system wrote every one of them.
// ---------------------------------------------------------------------------

// The span of text between a matching pair of brackets or braces, starting
// from the first one at or after `from`. Returns false if the pair is not
// found or does not close.
bool find_block(const std::string& text, size_t from,
                char open_ch, char close_ch,
                size_t& begin_out, size_t& end_out);

// The string value of a named field within [begin, end). Unescapes as it
// extracts. Returns false when the name is not present in that range.
bool read_string_field(const std::string& text, size_t begin, size_t end,
                       const std::string& name, std::string& out);

// The numeric value of a named field within [begin, end).
bool read_number_field(const std::string& text, size_t begin, size_t end,
                       const std::string& name, double& out);

// The boolean value of a named field within [begin, end).
bool read_bool_field(const std::string& text, size_t begin, size_t end,
                     const std::string& name, bool& out);

// Every quoted string inside the list named `name` within [begin, end).
// Returns false when the named list is not present; an empty list is Ok
// with an empty result.
bool read_string_list(const std::string& text, size_t begin, size_t end,
                      const std::string& name, std::vector<std::string>& out);

// Every quoted name that begins a record inside the object spanning
// [begin, end) — used to discover buckets without any list of them being
// maintained anywhere. The file's own contents are the list.
std::vector<std::string> read_object_keys(const std::string& text,
                                          size_t begin, size_t end);

// Escape a string for writing back out.
std::string escape_text(const std::string& s);

} // namespace prime
