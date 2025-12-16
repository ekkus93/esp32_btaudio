// Minimal stub for esp_rom_sys.h used in host tests
#ifndef MOCK_ESP_ROM_SYS_H
#define MOCK_ESP_ROM_SYS_H

static inline void esp_rom_delay_us(unsigned int us)
{
    (void)us;
}

#endif /* MOCK_ESP_ROM_SYS_H */