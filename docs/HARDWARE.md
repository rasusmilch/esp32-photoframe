# Hardware contract

This contract is derived from current board definitions. **Adopted** means required semantics; **source-derived** means inspected configuration; neither means physically validated. All physical results are currently **pending hardware validation**.

## Boards and controls

| Board ID | Display / rotation | Storage | Adopted semantic controls (source GPIO) | Status |
|---|---|---|---|---|
| `waveshare_photopainter_73` | 800×480 Spectra, 180° | SDIO: CLK39 CMD41 D0–D3 40/1/2/38 | BOOT0 short refresh/long clear; KEY4 next; dedicated previous unavailable | adopted + source-derived; pending |
| `seeedstudio_xiao_ee02` | 1200×1600, 0° | LittleFS | Button3 GPIO5 short refresh/long clear; Button2 GPIO3 previous; Button1 GPIO2 next | adopted + source-derived; pending |
| `seeedstudio_xiao_ee04` | 800×480, 0° | LittleFS | Button3 GPIO5 short refresh/long clear; Button2 GPIO3 previous; Button1 GPIO2 next | adopted + source-derived; pending |
| `seeedstudio_reterminal_e1002` | 800×480 Spectra, 0° | SPI SD CS14/PWR16, LittleFS fallback | green GPIO3 short refresh/long clear; left GPIO5 previous; right GPIO4 next | adopted + source-derived; priority pending |
| `seeedstudio_reterminal_e1003` | 1872×1404 GC16, 0° | SPI SD CS14/PWR39, LittleFS fallback | refresh GPIO3 short refresh/long clear; left GPIO5 previous; right GPIO4 next | adopted + source-derived; pending |
| `seeedstudio_reterminal_e1004` | 1200×1600 Spectra dual-CS, 0° | shared-SPI SD CS14/PWR16, LittleFS fallback | refresh GPIO5 short refresh/long clear; left GPIO4 previous; right GPIO3 next | adopted + source-derived; priority pending |

Buttons are source-defined active-low inputs and EXT1 wake candidates. Current runtime still interprets generic wake/rotate/clear roles; the table is accepted target behavior, not implemented behavior.

## Board-specific physical contract

- **Waveshare:** display SPI SCLK10/MOSI11, DC8/CS9/RST12/BUSY13; AXP2101 on I²C 47/48, IRQ21; battery/USB/charging via PMIC; no dedicated clear/previous physical key. Its simultaneous USB+battery stability warning remains source documentation, not validation.
- **XIAO EE02:** display SCLK7/MOSI9, DC10/CS44/CS1 41/RST38/BUSY4/EN43. Internal flash only; no external RTC in its HAL. USB detection relies on USB-Serial-JTAG host activity, so data-less chargers may be undetected.
- **XIAO EE04:** display SCLK7/MOSI9, DC10/CS44/RST38/BUSY4/EN43. Internal flash only; the same USB-detection limitation applies.
- **E1002:** display SCLK7/MOSI9/MISO8, DC11/CS10/RST12/BUSY13; RTC/sensor I²C 19/20; battery ADC GPIO1 gated by GPIO21; LED6 active-low. V1.2+ may expose SY6974B on separate I²C 39/40; earlier ETA6003 revisions lack that I²C capability, changing USB detection.
- **E1003:** IT8951/GC16 display CS10/RST12/BUSY13, enable11/VCC21; RTC/SHT40/SY6974B share I²C 19/20; battery ADC1 enable40; automatic light sleep is disabled by its board definition.
- **E1004:** shared display/SD SPI uses SCLK7/MOSI9/MISO8; display DC11, CS10/CS1 2, RST38, BUSY13, EN12. RTC/SHT40/SY6974B use I²C 19/20; battery ADC1 enable21. Automatic light sleep is disabled because shared-bus isolation can time out SD traffic.

Storage selects mounted SD first on SD-capable reTerminal/Waveshare boards and LittleFS where configured, then volatile MemFS. Power management enables timer wake for auto-rotation and EXT1 for available buttons, with board HAL preparation before deep sleep. GPIO polarity, wake masks, RTC, charger, sensor, USB, storage fallback, and panel behavior require per-board verification; never infer E1002 behavior for another board.
