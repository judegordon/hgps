// New here. docs/decisions/0022-hand-written-config-validation.md says the loader is the
// authority and schemas/v2 is the published contract; this is the mechanism that stops the two
// drifting apart. It reads the schema, resolves its local $refs, and for every `required`
// property asserts that the loader rejects a document missing it, and for every level with
// `additionalProperties: false` asserts that the loader rejects an unknown property there.
#include "config/loader.h"

#include "support/config_fixture.h"
#include "support/test_paths.h"

#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

using hgps::diag::IssueCode;
using hgps::diag::IssueReport;
using hgps::test::ConfigFixture;

nlohmann::json read_schema(const std::string &relative) {
    const auto path = hgps::test::schemas_dir() / "v2" / relative;
    std::ifstream stream{path};
    EXPECT_TRUE(stream) << "cannot open " << path.string();
    return nlohmann::json::parse(stream);
}

/// Inlines every local "$ref": "config/x.json" so the schema can be walked in one piece.
nlohmann::json resolve_refs(nlohmann::json node, const std::string &base_directory) {
    if (node.is_object()) {
        if (const auto ref = node.find("$ref"); ref != node.end() && ref->is_string()) {
            auto target = ref->get<std::string>();
            if (target.starts_with("http")) {
                return node;
            }

            std::string directory = base_directory;
            std::string file = target;
            if (const auto slash = target.rfind('/'); slash != std::string::npos) {
                directory = base_directory.empty()
                                ? target.substr(0, slash)
                                : base_directory + "/" + target.substr(0, slash);
                file = target.substr(slash + 1);
            }

            const auto relative = directory.empty() ? file : directory + "/" + file;
            return resolve_refs(read_schema(relative), directory);
        }

        nlohmann::json result = nlohmann::json::object();
        for (const auto &member : node.items()) {
            result[member.key()] = resolve_refs(member.value(), base_directory);
        }
        return result;
    }

    if (node.is_array()) {
        nlohmann::json result = nlohmann::json::array();
        for (const auto &element : node) {
            result.push_back(resolve_refs(element, base_directory));
        }
        return result;
    }

    return node;
}

/// One place in a document that the schema constrains.
struct SchemaSite {
    std::vector<std::string> path;     // the JSON path to the object
    std::vector<std::string> required; // its required members
    bool closed{false};                // additionalProperties: false
};

void collect_sites(const nlohmann::json &schema, std::vector<std::string> path,
                   std::vector<SchemaSite> &sites) {
    if (!schema.is_object()) {
        return;
    }

    SchemaSite site;
    site.path = path;

    if (const auto required = schema.find("required");
        required != schema.end() && required->is_array()) {
        for (const auto &name : *required) {
            site.required.push_back(name.get<std::string>());
        }
    }

    if (const auto additional = schema.find("additionalProperties");
        additional != schema.end() && additional->is_boolean()) {
        site.closed = !additional->get<bool>();
    }

    if (!site.required.empty() || site.closed) {
        sites.push_back(site);
    }

    if (const auto properties = schema.find("properties");
        properties != schema.end() && properties->is_object()) {
        for (const auto &member : properties->items()) {
            auto child = path;
            child.push_back(member.key());
            collect_sites(member.value(), child, sites);
        }
    }
}

nlohmann::json *navigate(nlohmann::json &document, const std::vector<std::string> &path) {
    nlohmann::json *node = &document;
    for (const auto &step : path) {
        if (!node->is_object() || node->find(step) == node->end()) {
            return nullptr;
        }
        node = &(*node)[step];
    }
    return node;
}

std::string describe(const std::vector<std::string> &path) {
    std::string result;
    for (const auto &step : path) {
        result += "/" + step;
    }
    return result.empty() ? "<root>" : result;
}

std::vector<SchemaSite> schema_sites() {
    const auto schema = resolve_refs(read_schema("config.json"), "");
    std::vector<SchemaSite> sites;
    collect_sites(schema, {}, sites);
    return sites;
}

bool loads(const ConfigFixture &fixture, const nlohmann::json &document, IssueReport &report) {
    return hgps::config::load_from_json(document, fixture.dir(), {}, report).has_value();
}

} // namespace

TEST(ConfigSchemaAgreement, TheSchemaFilesParseAndResolve) {
    const auto schema = resolve_refs(read_schema("config.json"), "");

    ASSERT_TRUE(schema.is_object());
    EXPECT_EQ(2, schema["properties"]["version"]["const"].get<int>());
    EXPECT_TRUE(schema["properties"]["project_requirements"].contains("properties"))
        << "the project_requirements $ref did not resolve";

    // Nothing should be left unresolved anywhere in the tree.
    const auto text = schema.dump();
    EXPECT_EQ(std::string::npos, text.find("\"$ref\""));
}

TEST(ConfigSchemaAgreement, TheFixtureDocumentIsValidAgainstTheSchemasRequiredSets) {
    const ConfigFixture fixture{"schema_agreement_fixture"};
    const auto document = fixture.document();

    for (const auto &site : schema_sites()) {
        auto copy = document;
        const auto *node = navigate(copy, site.path);
        if (node == nullptr) {
            // The site is optional in the fixture (individual_id_tracking, for instance).
            continue;
        }

        for (const auto &name : site.required) {
            EXPECT_TRUE(node->contains(name))
                << "the test fixture is missing " << describe(site.path) << "/" << name
                << ", which the schema says is required";
        }
    }
}

TEST(ConfigSchemaAgreement, EveryRequiredPropertyIsEnforcedByTheLoader) {
    const ConfigFixture fixture{"schema_agreement_required"};

    // Sites reached only through an optional parent cannot be tested this way: removing a
    // required member of an absent object changes nothing. Those are covered by the loader tests
    // directly.
    const std::set<std::string> optional_parents{"/output/individual_id_tracking",
                                                 "/population_impact_fraction"};

    for (const auto &site : schema_sites()) {
        if (optional_parents.contains(describe(site.path))) {
            continue;
        }

        for (const auto &name : site.required) {
            auto document = fixture.document();
            auto *node = navigate(document, site.path);
            if (node == nullptr || !node->contains(name)) {
                continue;
            }
            node->erase(name);

            IssueReport report;
            EXPECT_FALSE(loads(fixture, document, report))
                << "the loader accepted a document with no " << describe(site.path) << "/" << name
                << ", which schemas/v2 lists as required";
            EXPECT_TRUE(report.has_errors()) << describe(site.path) << "/" << name;
        }
    }
}

TEST(ConfigSchemaAgreement, EveryClosedObjectRejectsAnUnknownProperty) {
    const ConfigFixture fixture{"schema_agreement_closed"};

    // The two sites whose contents are deliberately open: demographic_models belongs to the
    // model that consumes it, and interventions/types is keyed by user-chosen names.
    const std::set<std::string> open_sites{"/modelling/demographic_models",
                                           "/running/interventions/types"};

    for (const auto &site : schema_sites()) {
        if (!site.closed || open_sites.contains(describe(site.path))) {
            continue;
        }

        auto document = fixture.document();
        auto *node = navigate(document, site.path);
        if (node == nullptr || !node->is_object()) {
            continue;
        }
        (*node)["zz_not_a_real_property"] = 1;

        IssueReport report;
        EXPECT_FALSE(loads(fixture, document, report))
            << "the loader accepted an unknown property at " << describe(site.path)
            << ", where schemas/v2 sets additionalProperties: false";
        EXPECT_TRUE(report.contains(IssueCode::config_unknown_property))
            << describe(site.path) << ": " << report.to_string();
    }
}

TEST(ConfigSchemaAgreement, TheSchemaAndTheLoaderAgreeOnTheRemovedV1Properties) {
    // The schema records these in a $comment; the loader rejects them by name. If one side
    // forgets, this test says which.
    const auto schema = resolve_refs(read_schema("config.json"), "");
    const auto comment = schema.value("$comment", std::string{});

    for (const auto *removed : {"trend_type", "income_categories", "sync_timeout_ms"}) {
        EXPECT_NE(std::string::npos, comment.find(removed))
            << "schemas/v2/config.json does not mention the removed property " << removed;
    }

    const ConfigFixture fixture{"schema_agreement_removed"};
    for (const auto *removed : {"trend_type", "income_categories"}) {
        auto document = fixture.document();
        document[removed] = "null";
        IssueReport report;
        EXPECT_FALSE(loads(fixture, document, report)) << removed;
        EXPECT_TRUE(report.contains(IssueCode::config_removed_property)) << removed;
    }

    auto document = fixture.document();
    document["running"]["sync_timeout_ms"] = 15000;
    IssueReport report;
    EXPECT_FALSE(loads(fixture, document, report));
    EXPECT_TRUE(report.contains(IssueCode::config_removed_property));
}
