/*
 * i2s_out — I2S master transmitter to the WROOM32 slave-RX (SPEC §3.3):
 * Philips, 16-bit data in 32-bit slots, stereo, 44.1 kHz, MCLK unused.
 * BCLK=GPIO5, WS=GPIO6, DOUT=GPIO7. Owns a PSRAM SPSC PCM ring; a writer
 * task drains it to the I2S channel, zero-fills + counts underruns.
 *
 * Implemented in SIG-1b. Skeleton only for now.
 */
#pragma once
