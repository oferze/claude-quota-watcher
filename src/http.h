#pragma once
#include <string>

#include "state.h"

// GET https://api.anthropic.com/api/oauth/usage with the given OAuth access token.
// Blocking; 15 s timeouts. Never puts the token in any output.
HttpOutcome HttpGetUsage(const std::string& accessToken);
