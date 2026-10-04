// config_writer.cpp — Prime Architect frontend config.json serializer implementation

#include "config_writer.h"

#include "text_file.h"

#include <map>
#include <sstream>

namespace prime {

namespace {

struct Bucket {
    std::vector<const AgentEntry*> agents;
    std::vector<const TeamEntry*>  teams;
};

void write_agent(std::ostringstream& o, const AgentEntry& a, const char* indent) {

    o << indent << "{\n";
    o << indent << "  \"id\": \""          << escape_text(a.id)          << "\",\n";
    o << indent << "  \"name\": \""        << escape_text(a.name)        << "\",\n";
    o << indent << "  \"kind\": \""        << escape_text(a.kind)        << "\",\n";
    o << indent << "  \"mapped_path\": \"" << escape_text(a.mapped_path) << "\",\n";
    o << indent << "  \"config_data\": {\n";
    o << indent << "    \"compute_target\": \""
      << escape_text(a.compute_target) << "\"\n";
    o << indent << "  }\n";
    o << indent << "}";
}

void write_team(std::ostringstream& o, const TeamEntry& t, const char* indent) {
    o << indent << "{\n";
    o << indent << "  \"id\": \""     << escape_text(t.id)     << "\",\n";
    o << indent << "  \"name\": \""   << escape_text(t.name)   << "\",\n";
    o << indent << "  \"kind\": \""   << escape_text(t.kind)   << "\",\n";
    o << indent << "  \"parent\": \"" << escape_text(t.parent) << "\",\n";
    o << indent << "  \"split_enabled\": "
      << (t.split_enabled ? "true" : "false") << ",\n";
    o << indent << "  \"team_roster\": [";
    for (size_t i = 0; i < t.roster.size(); ++i) {
        o << "\"" << escape_text(t.roster[i]) << "\"";
        if (i + 1 < t.roster.size()) o << ", ";
    }
    o << "]\n";
    o << indent << "}";
}

}

std::string ConfigWriter::to_json(const std::vector<AgentEntry>& agents,
                                  const std::vector<TeamEntry>& teams) {

    std::map<std::string, Bucket> by_department;

    for (const auto& a : agents) by_department[a.department].agents.push_back(&a);
    for (const auto& t : teams)  by_department[t.department].teams.push_back(&t);

    std::ostringstream o;
    o << "{\n";
    o << "  \"Departments\": {\n";

    size_t written = 0;
    for (const auto& [department, bucket] : by_department) {
        o << "    \"" << escape_text(department) << "\": {\n";

        o << "      \"agents\": [\n";
        for (size_t i = 0; i < bucket.agents.size(); ++i) {
            write_agent(o, *bucket.agents[i], "        ");
            if (i + 1 < bucket.agents.size()) o << ",";
            o << "\n";
        }
        o << "      ],\n";

        o << "      \"teams\": [\n";
        for (size_t i = 0; i < bucket.teams.size(); ++i) {
            write_team(o, *bucket.teams[i], "        ");
            if (i + 1 < bucket.teams.size()) o << ",";
            o << "\n";
        }
        o << "      ]\n";

        o << "    }";
        if (++written < by_department.size()) o << ",";
        o << "\n";
    }

    o << "  }\n";
    o << "}\n";
    return o.str();
}

bool ConfigWriter::write(const std::string& config_path,
                         const std::vector<AgentEntry>& agents,
                         const std::vector<TeamEntry>& teams) {
    return write_text_file(config_path, to_json(agents, teams));
}

}
