# tone — controllable test-tone fill source (WEB-1d / RADIO-2c). Pure fill; the
# audio_out arbiter (main) does the i2s writing.
idf_component_register(
    SRCS "tone.c"
    INCLUDE_DIRS "include"
    REQUIRES signal_gen)
