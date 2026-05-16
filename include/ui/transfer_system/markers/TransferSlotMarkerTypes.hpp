#pragma once

namespace pr {

/// Visual-only transfer grid markers (6×5 Resort / external save columns).
enum class TransferSlotMarkerKind {
    None = 0,
    /// Game column: Tier-0 home_tracker or Tier-1 valid OpenHome linkage.
    ReturnVisitor,
    /// Game column: Resort → game staging until save/discard.
    StagingFromResort,
    /// Resort column: Game → Resort first visit staging.
    FirstVisitStaging,
    /// Resort column: return visitor carried game → Resort this session.
    ReturnVisitorOnResortUntilSave,
};

enum class TransferMarkerPanel {
    GameColumn,
    ResortColumn,
};

struct TransferMarkerSessionView {
    bool yellow_on_game = false;
    bool blue_on_resort = false;
    bool green_carry_on_resort = false;
};

} // namespace pr
