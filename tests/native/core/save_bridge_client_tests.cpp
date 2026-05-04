#include "core/bridge/SaveBridgeClient.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

void testPrefersSpawnErrorOverStdout() {
    pr::SaveBridgeProbeResult r;
    r.exit_code = 1;
    r.error_message = "Failed to launch process: nope";
    r.stdout_text = R"({"error":"ignored","details":"x"})";
    expect(pr::formatBridgeRunFailureMessage(r) == "Failed to launch process: nope", "spawn error should win");
}

void testParsesStdoutJsonErrorAndDetails() {
    pr::SaveBridgeProbeResult r;
    r.exit_code = 1;
    r.launched = true;
    r.stdout_text =
        "{\"bridge_write_schema\":1,\"success\":false,\"status\":\"error\",\"error\":\"projection_apply_failed\","
        "\"details\":\"pc_boxes_len=32 expected_BoxCount=31\"}\n";
    expect(pr::formatBridgeRunFailureMessage(r) ==
               "projection_apply_failed: pc_boxes_len=32 expected_BoxCount=31",
           "should merge error + details");
}

void testExtractsJsonWhenStdoutHasPrefix() {
    pr::SaveBridgeProbeResult r;
    r.exit_code = 1;
    r.stdout_text = "noise\n{\"error\":\"missing_projection\",\"details\":\"/tmp/x.json\"}\n";
    expect(pr::formatBridgeRunFailureMessage(r) == "missing_projection: /tmp/x.json", "brace slice parse");
}

void testStderrFallback() {
    pr::SaveBridgeProbeResult r;
    r.exit_code = 1;
    r.stdout_text = "not json";
    r.stderr_text = "clr failure\n";
    expect(pr::formatBridgeRunFailureMessage(r).find("stderr:") == 0, "stderr fallback");
}

void testParsesBridgeProjectSuccess() {
    const std::string json = R"({
  "bridge_project_schema": 1,
  "success": true,
  "target_format_name": "PK5",
  "target_raw_payload_base64": "QQ==",
  "target_raw_hash_sha256": "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd",
  "target_pid": 3735928559,
  "legality": { "valid": true, "warnings": ["minor"] },
  "loss_manifest": {
    "lossy": true,
    "lost_categories": ["memories"],
    "projected_categories": ["species", "level"],
    "notes": ["Target cannot represent memories."]
  }
})";
    const pr::SaveBridgeProjectResult r = pr::parseBridgeProjectResultJson(json);
    expect(r.success, r.error_message);
    expect(r.target_format_name == "PK5", "target format");
    expect(r.target_raw_payload_base64 == "QQ==", "target payload");
    expect(r.legality_valid, "legality valid");
    expect(r.legality_warnings.size() == 1 && r.legality_warnings[0] == "minor", "legality warnings");
    expect(r.loss_manifest_lossy, "lossy manifest");
    expect(r.lost_categories.size() == 1 && r.lost_categories[0] == "memories", "lost categories");
    expect(r.projected_categories.size() == 2, "projected categories");
    expect(r.loss_notes.size() == 1, "loss notes");
    expect(r.target_pid.has_value() && *r.target_pid == 3735928559u, "target_pid");
}

void testBridgeProjectFailureCarriesMessage() {
    const std::string json = R"({
  "bridge_project_schema": 1,
  "success": false,
  "status": "unsupported",
  "error": "projection_not_implemented",
  "details": "pending"
})";
    const pr::SaveBridgeProjectResult r = pr::parseBridgeProjectResultJson(json);
    expect(!r.success, "expected project failure");
    expect(r.error_message == "projection_not_implemented: pending", "project failure message");
}

} // namespace

int main() {
    try {
        testPrefersSpawnErrorOverStdout();
        testParsesStdoutJsonErrorAndDetails();
        testExtractsJsonWhenStdoutHasPrefix();
        testStderrFallback();
        testParsesBridgeProjectSuccess();
        testBridgeProjectFailureCarriesMessage();
        std::cout << "save_bridge_client_tests: OK\n";
        return 0;
    } catch (const TestFailure& ex) {
        std::cerr << "save_bridge_client_tests: FAILED: " << ex.what() << "\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "save_bridge_client_tests: ERROR: " << ex.what() << "\n";
        return 2;
    }
}
