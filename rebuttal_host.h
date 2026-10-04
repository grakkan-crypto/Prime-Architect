// rebuttal_host.h — Rebuttal window host: THE owner of the rows
//
// ALL host logic lives HERE. The rows are private data behind real methods —
// add, set subject, append input, advance, remove — and nothing else reaches
// in. RebuttalTurn holds a reference to this file and tells it what to do; it
// never touches row data. The render reads only through this file. This host
// is ONLY the rebuttal window — nothing of the main chat window's surface is
// any of its business.
//
// SUBJECT
//   set_subject is called DIRECTLY by its writer (Adept-Infer derives the
//   subject on its own reach). No conduit, no forwarding entry, no file in
//   the middle repeating the fact.
//
// BOLD — the current row
//   One row is bold: the current one. A first row, being the only row, IS
//   the current one and is bold from creation — its id is readable off it
//   from that moment, the same way anything reads anything. advance() moves
//   bold strictly in creation order, wrapping. Removing the bold row passes
//   bold along that same ruled cycle order — the row now sitting where the
//   removed one was.
//
// THE TWO DISMISS DOORS — different initiating side, one identical close
//   BUTTON (UI first): dismiss(id) — this file tears the row down, then
//     tells RebuttalTurn the id and that it is closed. RebuttalTurn resolves.
//   CHAT (Adept-Infer recognising confirm/dismiss): RebuttalTurn acts first —
//     it reads the current row's id off this file (current_id), tells this
//     file the turn is dismissed (remove), and resolves. One motion, not two
//     calls bouncing a fact around.
//   The UI door needs RebuttalTurn reachable; both objects reference each
//   other, so that link lands once at wiring, after both exist.

#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace prime::frontend {

class RebuttalTurn;

class RebuttalHost {
public:
    RebuttalHost() = default;

    RebuttalHost(const RebuttalHost&)            = delete;
    RebuttalHost& operator=(const RebuttalHost&) = delete;

    void bind(RebuttalTurn& turn) { turn_ = &turn; }

    void add_row(const std::string& turn_id);

    void set_subject(const std::string& turn_id, std::string subject);

    void append_input(const std::string& turn_id, std::string text);

    void advance();

    void remove(const std::string& turn_id);

    void dismiss(const std::string& turn_id);

    std::string current_id() const;

    size_t row_count() const;

    struct Row {
        std::string              turn_id;
        std::string              subject;
        std::vector<std::string> inputs;
        bool                     bold = false;
    };
    std::vector<Row> rows() const;

private:
    mutable std::mutex mutex_;
    std::vector<Row>   rows_;
    RebuttalTurn*      turn_ = nullptr;
};

}
