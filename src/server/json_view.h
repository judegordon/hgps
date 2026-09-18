// Turning the engine's public types into the JSON shapes docs/server-api.md publishes.
//
// Kept apart from the HTTP layer on purpose: this is the part a test can check without opening a
// socket, and it is the part that has to stay in step with the document. Nothing here includes an
// engine internal — the server is a client of hgps::engine, exactly as the CLI is
// (docs/decisions/0032-library-and-a-thin-cli.md and
// docs/decisions/0042-a-local-server-in-the-same-binary.md).
#pragma once

#include "hgps/engine.h"

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace hgps::server {

/// @brief One diagnostic, as docs/server-api.md publishes it.
///
/// `location.field` is a JSON pointer wherever the engine knows one, and the wire name for it is
/// `pointer` — an editor uses it to put the message on the field it names rather than at the top
/// of a form, so the name says what it is for rather than what the struct calls it.
nlohmann::json to_json(const api::Diagnostic &diagnostic);

/// @brief Every diagnostic in a report, in the order they were found.
nlohmann::json to_json(const api::Report &report);

/// @brief What `GET /api/examples/{id}` calls `summary`: what the engine understood, not what a
///        reader would guess from the document.
nlohmann::json summary_of(const api::Configuration &configuration);

/// @brief `api::Run::Description`, for the response to a run that has just been built.
nlohmann::json to_json(const api::Run::Description &description);

/// @brief `api::build_info()` plus this document's own version and the compatibility flags this
///        build knows, so a client need not hard-code them.
nlohmann::json version_document(int api_version);

/// @brief The error envelope. One shape everywhere, so a client has one thing to handle.
nlohmann::json error_document(const std::string &code, const std::string &message,
                              const api::Report *report = nullptr);

/// @brief The published config schema with its `$ref`s resolved into one document.
///
/// A schema-driven form generator that has to fetch transitively is a form generator with a
/// loading state per field, so the resolution happens here, once.
///
/// @throws std::runtime_error if a `$ref` names a file that is not there, because a half-resolved
///         schema would generate a form with silently missing fields.
nlohmann::json inline_schema_refs(const std::filesystem::path &schema_path);

} // namespace hgps::server
