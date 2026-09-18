// Translates the engine's internal `diag::IssueReport` into the public `api::Report`.
//
// The two exist separately on purpose. The internal one carries a closed `IssueCode` enumeration,
// because a loader that can invent a code is a loader whose messages nobody tests; the public one
// carries the code as a string, so that adding an internal code does not change a published header
// or break a caller that switches on one it has never heard of
// (docs/decisions/0032-library-and-a-thin-cli.md).
#pragma once

#include "diagnostics/issue_report.h"
#include "hgps/diagnostics.h"

namespace hgps::api::detail {

Diagnostic to_public(const diag::InputIssue &issue);

/// @brief Appends every issue of an internal report to a public one, preserving their order.
void append_to_public(const diag::IssueReport &internal, Report &report);

} // namespace hgps::api::detail
