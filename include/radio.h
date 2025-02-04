#ifndef RADIO_H
#define RADIO_H

#include "esp_err.h"

// Function to start playing radio from a URL
esp_err_t radio_play(const char *url);

// Function to stop playing radio
esp
