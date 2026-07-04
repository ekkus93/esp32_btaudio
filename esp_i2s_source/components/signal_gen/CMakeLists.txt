# signal_gen — 44.1 kHz stereo s16 sine/sweep/silence producers (SIG-1a).
# Pure functions, host-tested.
idf_component_register(SRCS "signal_gen.c"
                       INCLUDE_DIRS "include")
