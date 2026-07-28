#include "bt_duplex_policy.h"

#include <string.h>

static bool valid_mode(bt_duplex_mode_t value)
{
    return (int)value >= 0 && value < BT_DUPLEX_MODE_COUNT;
}

static bool valid_reason(bt_duplex_policy_reason_t value)
{
    return (int)value >= 0 && value < BT_DUPLEX_POLICY_REASON_COUNT;
}

static void set_waiting_reason(const bt_duplex_policy_input_t *input,
                               bt_duplex_policy_result_t *result)
{
    if (!input->a2dp_connected) {
        result->state = BT_DUPLEX_POLICY_WAITING;
        result->reason = BT_DUPLEX_POLICY_REASON_WAITING_A2DP_CONNECTION;
    } else if (!input->a2dp_streaming) {
        result->state = BT_DUPLEX_POLICY_WAITING;
        result->reason = BT_DUPLEX_POLICY_REASON_WAITING_A2DP_STREAM;
    } else if (!input->hfp_slc_connected) {
        result->state = BT_DUPLEX_POLICY_WAITING;
        result->reason = BT_DUPLEX_POLICY_REASON_WAITING_HFP_SLC;
    } else if (!input->sco_connected) {
        result->state = BT_DUPLEX_POLICY_WAITING;
        result->reason = BT_DUPLEX_POLICY_REASON_WAITING_SCO;
    } else {
        result->state = BT_DUPLEX_POLICY_SATISFIED;
        result->reason = BT_DUPLEX_POLICY_REASON_REQUESTED_MODE;
    }
}

esp_err_t bt_duplex_policy_evaluate(const bt_duplex_policy_input_t *input,
                                    bt_duplex_policy_result_t *result_out)
{
    if (input == NULL || result_out == NULL || !valid_mode(input->requested) ||
        !valid_mode(input->current_effective) ||
        !valid_reason(input->interruption_reason)) {
        return ESP_ERR_INVALID_ARG;
    }

    bt_duplex_policy_result_t result;
    memset(&result, 0, sizeof(result));
    result.effective = input->requested;
    result.state = BT_DUPLEX_POLICY_SATISFIED;
    result.reason = BT_DUPLEX_POLICY_REASON_REQUESTED_MODE;
    result.downlink_owner = BT_DUPLEX_DOWNLINK_OWNER_A2DP;

    switch (input->requested) {
    case BT_DUPLEX_MODE_DISABLED:
        /* DISABLED means no HFP duplex policy. Existing A2DP behavior remains
         * authoritative and keeps sole ownership of the playback downlink. */
        result.effective = BT_DUPLEX_MODE_DISABLED;
        break;

    case BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC:
        result.effective = BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC;
        if (input->a2dp_interrupted_during_sco) {
            result.state = BT_DUPLEX_POLICY_INCOMPATIBLE;
            result.reason = input->interruption_reason;
        } else {
            set_waiting_reason(input, &result);
        }
        break;

    case BT_DUPLEX_MODE_HFP_FULL_DUPLEX:
        result.effective = BT_DUPLEX_MODE_HFP_FULL_DUPLEX;
        result.downlink_owner = BT_DUPLEX_DOWNLINK_OWNER_HFP;
        result.request_hfp_downlink = true;
        if (!input->hfp_slc_connected) {
            result.state = BT_DUPLEX_POLICY_WAITING;
            result.reason = BT_DUPLEX_POLICY_REASON_WAITING_HFP_SLC;
        } else if (!input->sco_connected) {
            result.state = BT_DUPLEX_POLICY_WAITING;
            result.reason = BT_DUPLEX_POLICY_REASON_WAITING_SCO;
        }
        break;

    case BT_DUPLEX_MODE_AUTO:
        if (input->a2dp_interrupted_during_sco && input->sco_connected) {
            result.effective = BT_DUPLEX_MODE_HFP_FULL_DUPLEX;
            result.state = BT_DUPLEX_POLICY_COMPATIBILITY_REQUIRED;
            result.reason = input->interruption_reason;
            result.downlink_owner = BT_DUPLEX_DOWNLINK_OWNER_HFP;
            result.request_hfp_downlink = true;
        } else {
            result.effective = BT_DUPLEX_MODE_A2DP_PLUS_HFP_MIC;
            result.downlink_owner = BT_DUPLEX_DOWNLINK_OWNER_A2DP;
            set_waiting_reason(input, &result);
            if (input->current_effective == BT_DUPLEX_MODE_HFP_FULL_DUPLEX) {
                result.reason = input->sco_connected
                    ? BT_DUPLEX_POLICY_REASON_A2DP_RESUMED
                    : BT_DUPLEX_POLICY_REASON_SCO_STOPPED;
            }
        }
        break;

    case BT_DUPLEX_MODE_COUNT:
    default:
        return ESP_ERR_INVALID_ARG;
    }

    *result_out = result;
    return ESP_OK;
}

const char *bt_duplex_policy_state_to_string(bt_duplex_policy_state_t value)
{
    switch (value) {
    case BT_DUPLEX_POLICY_SATISFIED: return "SATISFIED";
    case BT_DUPLEX_POLICY_WAITING: return "WAITING";
    case BT_DUPLEX_POLICY_INCOMPATIBLE: return "INCOMPATIBLE";
    case BT_DUPLEX_POLICY_COMPATIBILITY_REQUIRED:
        return "COMPATIBILITY_REQUIRED";
    default: return "UNKNOWN";
    }
}

const char *bt_duplex_policy_reason_to_string(bt_duplex_policy_reason_t value)
{
    switch (value) {
    case BT_DUPLEX_POLICY_REASON_REQUESTED_MODE: return "REQUESTED_MODE";
    case BT_DUPLEX_POLICY_REASON_WAITING_A2DP_CONNECTION:
        return "WAITING_A2DP_CONNECTION";
    case BT_DUPLEX_POLICY_REASON_WAITING_A2DP_STREAM:
        return "WAITING_A2DP_STREAM";
    case BT_DUPLEX_POLICY_REASON_WAITING_HFP_SLC:
        return "WAITING_HFP_SLC";
    case BT_DUPLEX_POLICY_REASON_WAITING_SCO: return "WAITING_SCO";
    case BT_DUPLEX_POLICY_REASON_REMOTE_SUSPENDED_A2DP_DURING_SCO:
        return "REMOTE_SUSPENDED_A2DP_DURING_SCO";
    case BT_DUPLEX_POLICY_REASON_A2DP_STOPPED_DURING_SCO:
        return "A2DP_STOPPED_DURING_SCO";
    case BT_DUPLEX_POLICY_REASON_A2DP_RESUMED: return "A2DP_RESUMED";
    case BT_DUPLEX_POLICY_REASON_SCO_STOPPED: return "SCO_STOPPED";
    default: return "UNKNOWN";
    }
}

const char *bt_duplex_downlink_owner_to_string(
    bt_duplex_downlink_owner_t value)
{
    switch (value) {
    case BT_DUPLEX_DOWNLINK_OWNER_A2DP: return "A2DP";
    case BT_DUPLEX_DOWNLINK_OWNER_HFP: return "HFP";
    default: return "UNKNOWN";
    }
}
