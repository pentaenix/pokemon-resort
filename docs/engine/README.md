# Engine Docs

Purpose: reusable runtime systems independent from gameplay and transfer domains.

Owned modules: rendering/input/audio/camera core/scene/chunk runtime scaffolding.

Dependencies: standard libraries + platform/runtime wrappers only.

Extension points: new engine subsystems must avoid gameplay rules.

Tests: add to `tests/native/core/*` or dedicated engine-native tests.
