# Oil pressure filter host test

The pressure filter has no ESP-IDF dependencies and can be checked on the host:

### POSIX shell (`sh`)

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -Iesp-data-hub-2/main/data_analog \
  esp-data-hub-2/main/data_analog/pressure_filter.c \
  esp-data-hub-2/test/test_pressure_filter.c \
  -lm -o pressure_filter_test
./pressure_filter_test
```

### Windows PowerShell

```powershell
gcc -std=c11 -Wall -Wextra -Werror `
  -Iesp-data-hub-2/main/data_analog `
  esp-data-hub-2/main/data_analog/pressure_filter.c `
  esp-data-hub-2/test/test_pressure_filter.c `
  -lm -o pressure_filter_test.exe
.\pressure_filter_test.exe
```

## RaceChrono packet encoder host test

### POSIX shell (`sh`)

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -Iesp32-shared/include \
  -Iesp-data-hub-2/main/racechrono \
  esp-data-hub-2/main/racechrono/racechrono_packet.c \
  esp-data-hub-2/test/test_racechrono_packet.c \
  -lm -o racechrono_packet_test
./racechrono_packet_test
```

### Windows PowerShell

```powershell
gcc -std=c11 -Wall -Wextra -Werror `
  -Iesp32-shared/include `
  -Iesp-data-hub-2/main/racechrono `
  esp-data-hub-2/main/racechrono/racechrono_packet.c `
  esp-data-hub-2/test/test_racechrono_packet.c `
  -lm -o racechrono_packet_test.exe
.\racechrono_packet_test.exe
```

## ISO-TP codec host test

The hardware-independent frame segmentation and reassembly logic is in
`isotp_codec.c`; queue-based flow control remains in `isotp.c`.

### POSIX shell (`sh`)

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -Iesp-data-hub-2/main/data_canbus \
  esp-data-hub-2/main/data_canbus/isotp_codec.c \
  esp-data-hub-2/test/test_isotp_codec.c \
  -o isotp_codec_test
./isotp_codec_test
```

### Windows PowerShell

```powershell
gcc -std=c11 -Wall -Wextra -Werror `
  -Iesp-data-hub-2/main/data_canbus `
  esp-data-hub-2/main/data_canbus/isotp_codec.c `
  esp-data-hub-2/test/test_isotp_codec.c `
  -o isotp_codec_test.exe
.\isotp_codec_test.exe
```

## Subaru SSM payload host test

### POSIX shell (`sh`)

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -Iesp-data-hub-2/main/data_canbus \
  esp-data-hub-2/main/data_canbus/request_ecu.c \
  esp-data-hub-2/test/test_request_ecu.c \
  -lm -o request_ecu_test
./request_ecu_test
```

### Windows PowerShell

```powershell
gcc -std=c11 -Wall -Wextra -Werror `
  -Iesp-data-hub-2/main/data_canbus `
  esp-data-hub-2/main/data_canbus/request_ecu.c `
  esp-data-hub-2/test/test_request_ecu.c `
  -lm -o request_ecu_test.exe
.\request_ecu_test.exe
```
