# i2s_out — SPSC PCM ring (SIG-1b) + I2S master-TX channel/writer task.
# pcm_ring is pure/host-tested; the I2S driver glue lands next in SIG-1b.
idf_component_register(SRCS "pcm_ring.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_psram)
