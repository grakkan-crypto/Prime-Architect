// input_render.h — The input surface.
//
// QUESTIONS BOX
//   A separate box directly above the typing box. Grows as questions are added,
//   shrinks as they're answered, gone entirely when there are none.
//
// NO SEND BUTTON
//   Nothing needs starting — the Architect is working from the first keystroke.
//   COMMIT is the user saying their turn is done.
//
// THE TYPING BOX IS NEVER DISABLED.
//
// ABORT
//   Its own button, shown while this pipeline's agents are working. Emptying
//   the typing box completely also aborts.

#pragma once

#include <functional>
#include <string>

#include "ingestion_state.h"

namespace prime {

using CommitFn         = std::function<void()>;
using AbortFn          = std::function<void()>;
using AnswerQuestionFn = std::function<void(uint32_t number, const std::string& answer)>;

class InputRender {
public:
    InputRender(prime::frontend::IngestionState& ingestion,
                CommitFn                         commit,
                AbortFn                          abort,
                AnswerQuestionFn                 answer_question);

    void draw(bool busy);

private:
    void draw_questions_box();

    prime::frontend::IngestionState& ingestion_;
    CommitFn                         commit_;
    AbortFn                          abort_;
    AnswerQuestionFn                 answer_question_;

    char     answer_[512]         = {0};
    uint32_t answering_           = 0;
    bool     had_text_last_frame_ = false;
};

}
