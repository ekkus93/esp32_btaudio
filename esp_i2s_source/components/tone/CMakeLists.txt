# tone — controllable test-tone source feeding i2s_out (WEB-1d).
idf_component_register(
    SRCS "tone.c"
    INCLUDE_DIRS "include"
    REQUIRES signal_gen i2s_out)
