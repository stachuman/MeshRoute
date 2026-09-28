#include "/tmp/w4b-final-layout-t3rLrA/layout/firmware_ui_chrome.h"
struct OutcomeView {
    mrui::Emergency    st         = mrui::Emergency::idle;
    mrui::RefuseReason refuse     = mrui::RefuseReason::other;
    // ★ THE THREE ALPHABETS OF A FAILURE, all frozen together (spec §2.1 rule 6): `refuse` is the compact panel code,
    //   §B73's `fail` is the CORE `SendFailReason` verbatim for an ASYNC failure, and UI-7's `refuse_code` is the
    //   SYNCHRONOUS `CmdCode` verbatim — needed because five different walls all return `err_unsupported` and the
    //   compact reason therefore cannot name them (see UiModel::on_send_refused).
    mrui::DmState      dm         = mrui::DmState::idle;
    mrui::ChanState    chan       = mrui::ChanState::idle;
    mrui::FailReason   fail       = mrui::FailReason::none;
    MESHROUTE_NS::CmdCode refuse_code = MESHROUTE_NS::CmdCode::queued;
    // ★★ §B69: WHICH channel outcome this alarm actually got. `Emergency::not_heard` alone cannot say, and the two
    //    readings are different claims — see firmware_ui_model.h's EmgEvidence.
    mrui::EmgEvidence  evidence   = mrui::EmgEvidence::none;
    uint8_t            arm_secs   = 0;
    // ★★★ §B115 — TWO FIELDS, NOT ONE, AND THE SPLIT IS THE FIX. `tries` is the model's `_tries` verbatim: ACCEPTED
    //     transmissions, the value the airtime bound is evaluated on, and what `NOT HEARD` reports because there the
    //     number IS the measurement. `attempt_ordinal` is "which attempt is in flight", which is a DIFFERENT question
    //     — see firmware_ui_model.h's two-numbers block. The shipped bug was one field serving both: the FIRING arm
    //     rendered `tries + 1` unconditionally, so the panel read `2 of 3` -> `3 of 3` -> `4 of 3` against three posts
    //     and `1 of 3` was never shown. ⛔ Do not re-merge them, and do not clamp either.
    uint8_t            tries      = 0;
    uint8_t            attempt_ordinal = 0;
    uint32_t           retry_in_s = 0;
    char               who[mrui::kLabelCap + 1] = {};
    char               text[21]                 = {};
};
struct SettingsView {
    bool open = false, unsaved = false, conflict = false, reboot = false;
    mrfw::CfgValues draft{};
};
extern "C" {
char mr_abi_size__mrui_UiState[sizeof(mrui::UiState)];
char mr_abi_align__mrui_UiState[alignof(mrui::UiState)];
char mr_abi_size__mrui_UiSnapshot[sizeof(mrui::UiSnapshot)];
char mr_abi_align__mrui_UiSnapshot[alignof(mrui::UiSnapshot)];
char mr_abi_size__mrui_UiModel[sizeof(mrui::UiModel)];
char mr_abi_align__mrui_UiModel[alignof(mrui::UiModel)];
char mr_abi_size__mrui_UiChrome[sizeof(mrui::UiChrome)];
char mr_abi_align__mrui_UiChrome[alignof(mrui::UiChrome)];
char mr_abi_size__mrui_FrameGate[sizeof(mrui::FrameGate)];
char mr_abi_align__mrui_FrameGate[alignof(mrui::FrameGate)];
char mr_abi_size__mrui_TeamRow[sizeof(mrui::TeamRow)];
char mr_abi_align__mrui_TeamRow[alignof(mrui::TeamRow)];
char mr_abi_size__mrui_InviteMember[sizeof(mrui::InviteMember)];
char mr_abi_align__mrui_InviteMember[alignof(mrui::InviteMember)];
char mr_abi_size__mrui_InviteIdRows[sizeof(mrui::InviteIdRows)];
char mr_abi_align__mrui_InviteIdRows[alignof(mrui::InviteIdRows)];
char mr_abi_size__OutcomeView[sizeof(OutcomeView)];
char mr_abi_align__OutcomeView[alignof(OutcomeView)];
char mr_abi_size__SettingsView[sizeof(SettingsView)];
char mr_abi_align__SettingsView[alignof(SettingsView)];
char mr_abi_size__mrui_HomeCapture[sizeof(mrui::HomeCapture)];
char mr_abi_align__mrui_HomeCapture[alignof(mrui::HomeCapture)];
}
