# Oil pressure filter host test

The pressure filter has no ESP-IDF dependencies and can be checked on the host:

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -Iesp-data-hub-2/main/data_analog \
  esp-data-hub-2/main/data_analog/pressure_filter.c \
  esp-data-hub-2/test/test_pressure_filter.c \
  -lm -o pressure_filter_test
./pressure_filter_test
```
