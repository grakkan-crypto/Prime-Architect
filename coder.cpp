// coder.cpp — the Coder pipeline file implementation
//
// Constant data, then the one wake. The roster and the pool table below
// ARE the declaration — complete and final before anything runs, copied
// out verbatim at wake time, never built, never decided (Rule 7).
//
// The pool table is the verified table, one pool per block. Each pool: its
// name, how many masks it carries, its mask triggers (3 bits per mask, in
// mask order), then every agent with access and that agent's bits (2
// access bits, then 3 bits per mask, in mask order). Every literal is
// written at its full width and aligned, so the column IS the check: a
// short literal is the same number but not the same line. The meaning of
// each digit is LiveRegistry's key, not this file's; this file states the
// digits and nothing about them.

#include "coder.h"

#include "project_ingest.h"

#include <thread>

namespace prime {

bool present(const std::string& target);

}

namespace prime::pipelines::coder {

namespace {

const std::vector<std::string> kRoster = {
    "Adept-Analyst",
    "Adept-Coder",
    "Adept-Coder-Split",
    "Adept-Infer",
    "Adept-Scout",
    "Aether",
    "Agent",
    "Analyst-Adept",
    "Analyst-Coder",
    "Analyst-Tool",
    "Arbiter-AdeptAnalyst",
    "Arbiter-AdeptAnalystCoT",
    "Arbiter-AdeptCoder",
    "Arbiter-AdeptCoT",
    "Arbiter-AdeptScout",
    "Arbiter-AdeptScoutCoT",
    "Arbiter-AnalystAdept",
    "Arbiter-AnalystCoder",
    "Arbiter-ArchitectCoT",
    "Arbiter-ArchitectIngest",
    "Arbiter-ArchitectSynth",
    "Arbiter-Auditor",
    "Architect-Ingest",
    "Architect-Synth",
    "Archivist-CoT",
    "Archivist-Ingest",
    "Archivist-Synth",
    "Auditor",
    "Author",
};

const std::vector<PoolDeclaration> kPools = {

    {"SHARED_CONTEXT", 0, 0b0, {
        {"Aether",                   0b10},
        {"Analyst-Tool",             0b10},
        {"Archivist-Ingest",         0b10},
        {"Analyst-Coder",            0b10},
        {"Architect-Ingest",         0b00},
        {"Adept-Coder",              0b00},
        {"Adept-Coder-Split",        0b00},
        {"Adept-Scout",              0b00},
        {"Adept-Analyst",            0b00},
        {"Analyst-Adept",            0b00},
        {"Arbiter-AnalystCoder",     0b00},
        {"Arbiter-AnalystAdept",     0b00},
        {"Arbiter-AdeptAnalyst",     0b00},
        {"Arbiter-AdeptScout",       0b00},
        {"Arbiter-AdeptCoder",       0b00},
    }},

    {"USER_INPUT", 0, 0b0, {
        {"Architect-Ingest",         0b10},
        {"Adept-Infer",              0b10},
        {"Aether",                   0b00},
        {"Analyst-Tool",             0b00},
        {"Archivist-Ingest",         0b00},
        {"Arbiter-ArchitectIngest",  0b00},
    }},

    {"SHARED_PROMPT", 1, 0b001, {
        {"Architect-Ingest",         0b10000},
        {"Analyst-Coder",            0b00111},
        {"Adept-Scout",              0b00110},
        {"Arbiter-AnalystCoder",     0b00110},
        {"Arbiter-AdeptScout",       0b00110},
        {"Arbiter-ArchitectIngest",  0b01000},
    }},

    {"ANALYST_INPUT", 1, 0b001, {
        {"Architect-Ingest",         0b10000},
        {"Analyst-Coder",            0b00111},
        {"Arbiter-ArchitectIngest",  0b01000},
        {"Arbiter-AnalystCoder",     0b00110},
    }},

    {"ANALYST_OUTPUT", 1, 0b101, {
        {"Analyst-Coder",            0b10000},
        {"Auditor",                  0b00110},
        {"Agent",                    0b00110},
        {"Architect-Synth",          0b00010},
        {"Archivist-Synth",          0b00010},
        {"Arbiter-ArchitectSynth",   0b00010},
        {"Arbiter-Auditor",          0b00110},
        {"Arbiter-AnalystCoder",     0b01001},
    }},

    {"ADEPT_INPUT", 2, 0b001010, {
        {"Architect-Ingest",         0b10000000},
        {"Adept-Coder",              0b00111000},
        {"Adept-Coder-Split",        0b00110110},
        {"Arbiter-AdeptCoder",       0b00110000},
        {"Arbiter-ArchitectIngest",  0b01000000},
    }},

    {"ADEPT_COT", 0, 0b0, {
        {"Adept-Coder",              0b10},
        {"Adept-Coder-Split",        0b00},
        {"Arbiter-AnalystAdept",     0b00},
        {"Arbiter-AdeptCoT",         0b01},
        {"Archivist-CoT",            0b01},
        {"Analyst-Adept",            0b01},
    }},

    {"ADEPT_SPLIT_COT", 0, 0b0, {
        {"Adept-Coder-Split",        0b10},
        {"Adept-Coder",              0b00},
        {"Arbiter-AnalystAdept",     0b00},
        {"Arbiter-AdeptCoT",         0b01},
        {"Archivist-CoT",            0b01},
        {"Analyst-Adept",            0b01},
    }},

    {"ADEPT_OUTPUT", 0, 0b0, {
        {"Adept-Coder",              0b10},
        {"Adept-Scout",              0b10},
        {"Architect-Synth",          0b00},
        {"Arbiter-AdeptCoder",       0b01},
        {"Archivist-Synth",          0b00},
        {"Arbiter-ArchitectSynth",   0b00},
    }},

    {"ARCHITECT_COT", 0, 0b0, {
        {"Architect-Ingest",         0b10},
        {"Adept-Infer",              0b00},
        {"Archivist-Ingest",         0b00},
        {"Aether",                   0b00},
        {"Analyst-Tool",             0b00},
        {"Arbiter-ArchitectCoT",     0b01},
    }},

    {"USER_OUTPUT", 0, 0b0, {
        {"Architect-Synth",          0b10},
        {"Arbiter-ArchitectSynth",   0b01},
    }},

    {"AUDITOR_OUTPUT", 0, 0b0, {
        {"Auditor",                  0b10},
        {"Arbiter-Auditor",          0b01},
    }},

    {"AUTHOR_OUTPUT", 1, 0b001, {
        {"Author",                   0b10000},
        {"Archivist-CoT",            0b00111},
    }},

    {"AETHER_INPUT", 0, 0b0, {
        {"Aether",                   0b00},
    }},

    {"ANALYST_ADEPT_OUTPUT", 0, 0b0, {
        {"Analyst-Adept",            0b10},
        {"Adept-Coder",              0b00},
        {"Adept-Coder-Split",        0b00},
        {"Arbiter-AnalystAdept",     0b01},
    }},

    {"ADEPT_ANALYST_INPUT", 0, 0b0, {
        {"Analyst-Coder",            0b10},
        {"Adept-Analyst",            0b00},
        {"Arbiter-AdeptAnalyst",     0b00},
        {"Arbiter-AnalystCoder",     0b01},
    }},

    {"ADEPT_ANALYST_COT", 0, 0b0, {
        {"Adept-Analyst",            0b10},
        {"Analyst-Coder",            0b00},
        {"Arbiter-AdeptAnalystCoT",  0b01},
        {"Arbiter-AnalystCoder",     0b00},
    }},

    {"ARCHIVIST_WORKBENCH", 0, 0b0, {
        {"Archivist-CoT",            0b10},
    }},

    {"PERSONALISATION_CONTEXT", 0, 0b0, {
        {"Archivist-Synth",          0b10},
        {"Architect-Synth",          0b00},
    }},

    {"PROJECT", 2, 0b001001, {
        {"Analyst-Coder",            0b00111000},
        {"Adept-Coder",              0b00000111},
        {"Adept-Coder-Split",        0b00000110},
        {"Adept-Scout",              0b00110000},
        {"Analyst-Adept",            0b00000110},
        {"Adept-Analyst",            0b00110000},
        {"Auditor",                  0b00110000},
        {"Arbiter-AnalystCoder",     0b00110000},
        {"Arbiter-AdeptCoder",       0b00000110},
        {"Arbiter-Auditor",          0b00110000},
        {"Arbiter-AnalystAdept",     0b00000110},
        {"Arbiter-AdeptAnalyst",     0b00110000},
    }},

    {"ADEPT_SCOUT_COT", 1, 0b001, {
        {"Adept-Scout",              0b10000},
        {"Analyst-Coder",            0b00111},
        {"Arbiter-AdeptScoutCoT",    0b01000},
        {"Arbiter-AnalystCoder",     0b00110},
    }},

    {"SCOUT_AID", 0, 0b0, {
        {"Analyst-Coder",            0b10},
        {"Adept-Scout",              0b00},
        {"Arbiter-AnalystCoder",     0b01},
    }},
};

const PipelinePayload kPayload = {kRoster, kPools};

}

PipelinePayload pipeline() {

    const bool wellness_check_project_ingest_present = present("ProjectIngest");
    (void)wellness_check_project_ingest_present;

    std::thread([]() {
        ProjectIngest{}.load();
    }).detach();

    return kPayload;
}

}
