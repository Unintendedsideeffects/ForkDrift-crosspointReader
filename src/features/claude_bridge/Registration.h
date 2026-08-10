#pragma once

#include <FeatureFlags.h>

#include <string>
#include <vector>

namespace features::claude_bridge {

struct QuestionOption {
  std::string label;
  std::string description;
};

struct ClaudeQuestion {
  std::string text;
  std::string header;
  std::vector<QuestionOption> options;
};

struct PendingRequest {
  std::string id;
  std::string sessionTitle;
  std::string cwd;
  std::vector<ClaudeQuestion> questions;
};

struct QuestionAnswer {
  std::string question;
  std::string value;
};

void registerFeature();

// The X4 is the HTTP server. Claude Code starts a short-lived PreToolUse hook
// for AskUserQuestion, pushes the questions here, waits for the button choices,
// and returns them through updatedInput.answers. No standing host daemon is
// required.
void attachActivity();
void detachActivity();

bool isConfigured();
bool peekPending(PendingRequest& out);
bool isPending(const std::string& id);
bool submitAnswers(const std::string& id, const std::vector<QuestionAnswer>& answers);
bool cancelPending(const std::string& id);

}  // namespace features::claude_bridge
