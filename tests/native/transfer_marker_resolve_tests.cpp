#include "core/domain/PcSlotSpecies.hpp"
#include "ui/transfer_system/markers/TransferMarkerResolve.hpp"

#include <cassert>

int main() {
    using pr::PcSlotSpecies;
    using pr::TransferMarkerPanel;
    using pr::TransferMarkerSessionView;
    using pr::TransferSlotMarkerKind;
    using pr::baselineReturnVisitorOnGame;
    using pr::resolveTransferSlotMarker;
    using pr::tier0ReturnVisitorCart;
    using pr::transferMarkerSessionAliasKeys;
    using pr::transferSlotMarkerIdentityKey;

    PcSlotSpecies hot{};
    hot.present = true;
    hot.slug = "bulbasaur";
    hot.home_tracker = "00000000-0000-4000-8000-000000000001";
    assert(tier0ReturnVisitorCart(hot));
    assert(transferSlotMarkerIdentityKey(hot).rfind("hot:", 0) == 0);

    PcSlotSpecies pkr{};
    pkr.present = true;
    pkr.slug = "x";
    pkr.resort_pkrid = "abc123";
    assert(transferSlotMarkerIdentityKey(pkr) == "pkrid:abc123");

    PcSlotSpecies pid{};
    pid.present = true;
    pid.slug = "y";
    pid.pid = 0x12345678u;
    pid.encryption_constant = 0xabcdef00u;
    assert(transferSlotMarkerIdentityKey(pid).rfind("pid_ec:", 0) == 0);

    PcSlotSpecies dual{};
    dual.present = true;
    dual.slug = "dual";
    dual.resort_pkrid = "resort_row";
    dual.pid = 0x11223344u;
    dual.encryption_constant = 0x55667788u;
    dual.ot_name = "Ash";
    const auto aliases = transferMarkerSessionAliasKeys(dual);
    assert(aliases.size() == 2u);
    assert(aliases[0] == "pkrid:resort_row");
    assert(aliases[1].rfind("pid_ec:", 0) == 0);
    assert(aliases[1].find("|ot:Ash") != std::string::npos);

    TransferMarkerSessionView none{};
    assert(resolveTransferSlotMarker(TransferMarkerPanel::GameColumn, hot, none, false, false) == TransferSlotMarkerKind::ReturnVisitor);

    TransferMarkerSessionView yellow{};
    yellow.yellow_on_game = true;
    assert(
        resolveTransferSlotMarker(TransferMarkerPanel::GameColumn, hot, yellow, false, false) ==
        TransferSlotMarkerKind::StagingFromResort);

    PcSlotSpecies tier1{};
    tier1.present = true;
    tier1.slug = "z";
    tier1.resort_pkrid = "pk";
    assert(baselineReturnVisitorOnGame(tier1, false, false) == false);
    assert(baselineReturnVisitorOnGame(tier1, true, false) == true);

    PcSlotSpecies profile_only{};
    profile_only.present = true;
    profile_only.slug = "w";
    profile_only.pid = 1u;
    profile_only.encryption_constant = 2u;
    profile_only.ot_name = "A";
    assert(baselineReturnVisitorOnGame(profile_only, false, true) == true);
    assert(baselineReturnVisitorOnGame(profile_only, false, false) == false);

    PcSlotSpecies resort_mon{};
    resort_mon.present = true;
    resort_mon.slug = "a";
    TransferMarkerSessionView blue{};
    blue.blue_on_resort = true;
    assert(
        resolveTransferSlotMarker(TransferMarkerPanel::ResortColumn, resort_mon, blue, false, false) ==
        TransferSlotMarkerKind::FirstVisitStaging);

    TransferMarkerSessionView carry{};
    carry.green_carry_on_resort = true;
    carry.blue_on_resort = true;
    assert(
        resolveTransferSlotMarker(TransferMarkerPanel::ResortColumn, resort_mon, carry, false, false) ==
        TransferSlotMarkerKind::ReturnVisitorOnResortUntilSave);

    return 0;
}
