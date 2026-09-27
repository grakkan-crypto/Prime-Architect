// chat_render.h — The conversation window.
//
// A normal chat window. Your prompt goes in on Commit, the reply comes in
// under it. Repeat.
//
// Holds lines and draws them. Nothing else.

#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace prime {

class ChatRender {
public:
    void add_user(const std::string& text);
    void add_ai(const std::string& text);
    void clear();

    void draw();

private:
    struct Line {
        bool        from_user;
        std::string text;
    };

    mutable std::mutex mutex_;
    std::vector<Line>  lines_;
};

} // namespace prime
