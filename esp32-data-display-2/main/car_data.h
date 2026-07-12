#include <stdbool.h>

#include "telemetry_types.h"

bool get_data(vehicle_state_t* packet);
void dd_car_data_uart_resync(void);
