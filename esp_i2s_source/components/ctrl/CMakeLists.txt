# ctrl — boot orchestration (CTRL-1). ctrl_cfg: NVS config store; ctrl_sm: pure
# orchestrator FSM (host-tested); ctrl: device glue task driving bt_link/radio.
idf_component_register(
    SRCS "ctrl_cfg.c" "ctrl_sm.c" "ctrl.c"
    INCLUDE_DIRS "include"
    REQUIRES bt_link wifi_mgr radio
    PRIV_REQUIRES nvs_flash)
