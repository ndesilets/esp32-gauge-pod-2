# Telemetry protocol host test

The codec is platform-independent and can be checked without ESP-IDF:

## POSIX shell (`sh`)

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -DMPACK_NODE=0 -DMPACK_BUILDER=0 \
  -Iesp32-shared/include -Iesp32-shared/third_party/mpack \
  esp32-shared/src/telemetry_protocol.c \
  esp32-shared/third_party/mpack/mpack.c \
  esp32-shared/test/test_telemetry_protocol.c \
  -o telemetry_protocol_test
./telemetry_protocol_test
```

## Windows PowerShell

```powershell
gcc -std=c11 -Wall -Wextra -Werror `
  -DMPACK_NODE=0 -DMPACK_BUILDER=0 `
  -Iesp32-shared/include -Iesp32-shared/third_party/mpack `
  esp32-shared/src/telemetry_protocol.c `
  esp32-shared/third_party/mpack/mpack.c `
  esp32-shared/test/test_telemetry_protocol.c `
  -o telemetry_protocol_test.exe
.\telemetry_protocol_test.exe
```
