# i2s_out — SPSC PCM ring (pcm_ring) + pure pump (i2s_out_pump) + I2S master-TX
# channel/writer task (i2s_out). SIG-1b. pcm_ring + pump are host-tested.
idf_component_register(SRCS "pcm_ring.c" "i2s_out_pump.c" "i2s_out.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_psram)
