/*
 * bt_link — UART1 client speaking the WROOM32 command protocol
 * (STATUS|COMMAND|RESULT[|DATA]). One in-flight command with timeout,
 * OK|/ERR| correlation, EVENT| fan-out to subscribers. S3 UART1
 * TX=GPIO17/RX=GPIO18 <-> WROOM32 UART2. C port of esp32_serial.py.
 *
 * Implemented in LINK-1. Skeleton only for now.
 */
#pragma once
