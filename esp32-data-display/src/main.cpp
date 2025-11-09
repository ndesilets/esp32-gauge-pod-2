#include <Arduino.h>
#include <Arduino_GFX_Library.h>

Arduino_DataBus* bus =
    new Arduino_ESP32QSPI(45 /* CS */, 47 /* SCK */, 21 /* D0 */, 48 /* D1 */,
                          40 /* D2 */, 39 /* D3 */);

Arduino_GFX* g = new Arduino_AXS15231B(
    bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, false /* IPS */,
    640 /* width */, 172 /* height */, 0 /* col offset 1 */,
    0 /* row offset 1 */, 0 /* col offset 2 */, 0 /* row offset 2 */,
    axs15231b_320480_init_operations, sizeof(axs15231b_320480_init_operations));

// then use it
void setup() {
  g->begin();
  g->fillScreen(YELLOW);
}