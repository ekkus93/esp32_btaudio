# ctrl — boot orchestration (CTRL-1). ctrl_cfg: NVS config store (sink MAC,
# autostart, last station). Orchestrator FSM + device glue added in CTRL-1b.
idf_component_register(
    SRCS "ctrl_cfg.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash)
