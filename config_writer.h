// config_writer.h — Prime Architect config serializer
//
// The mechanical write. Takes the authored agent and team entries and stamps
// the config file in the exact shape LiveRegistry reads back. No model in the
// loop, no review boundary — this is the operator's own authored state going
// to disk.
//
// NO JSON. NO PARSER. NO TRANSLATION LAYER.
//   This file is the system writing its own facts down for itself to read
//   again. It is not a document for a stranger, so it does not need a general
//   format with braces to scan and characters to escape.
//
//   A fixed-size value is written as itself. Anything that is not fixed size
//   is preceded by its byte length — that prefix is the ONLY concession to
//   structure, and it exists because a name that is five bytes today and nine
//   tomorrow has no other way to say where it ends.
//
// THREE FLAT SECTIONS — THE SAME SHAPE LiveRegistry HOLDS IN MEMORY
//
//   [u32 agent_count]
//     per agent:  id, name, gguf_path, compute_target        (each length-prefixed)
//   [u32 team_count]
//     per team:   id, name, parent, [u8 split_enabled],
//                 [u32 member_count], member names           (each length-prefixed)
//   [u32 split_parent_count]
//     per split:  parent_name, [u32 runner_count], runner names
//
//   Reading it is filling that layout directly. There is no second
//   representation in between and nothing to reconcile.
//
// DEPARTMENT IS NOT WRITTEN, ANYWHERE
//   An agent's department is the leading word of its own name, always, by
//   naming convention. It was previously stated by which bucket an entry sat
//   in; now it is not stated at all, because the name already says it. One
//   fact, one place, and that place is the name.
//
//   The department buckets are gone with it. A team's roster may still only
//   name members from its own department plus Arbiter — that is an authoring
//   rule, enforced in the authoring layer, and it no longer needs the file's
//   shape to carry it.
//
// THE SPLIT SECTION IS DERIVED HERE, NOT AUTHORED
//   A split parent's runners are: the parent itself first, then its
//   duplicates in creation order. The duplicates are already in `teams` (any
//   team whose `parent` names this one), so this section is worked out at
//   write time rather than authored or stored separately.
//
//   It is written to disk anyway, despite being derivable, because the
//   registry's three sections and the file's three sections are deliberately
//   the same three sections. Deriving it once here beats deriving it on every
//   load.
//
// DETERMINISTIC ORDERING
//   Agents then teams in insertion order; split parents in team order. A save
//   that changed nothing produces a byte-identical file.

#pragma once

#include <string>
#include <vector>

#include "agent_config.h"
#include "team_config.h"

namespace prime {

class ConfigWriter {
public:
    // Serialize to the exact byte layout LiveRegistry::load() reads.
    static std::string to_blob(const std::vector<AgentEntry>& agents,
                               const std::vector<TeamEntry>& teams);

    // Serialize and write. Returns false on failure so the caller can surface
    // it — a failed save is a visible failure, never a silent no-op.
    static bool write(const std::string& config_path,
                      const std::vector<AgentEntry>& agents,
                      const std::vector<TeamEntry>& teams);
};

} // namespace prime
