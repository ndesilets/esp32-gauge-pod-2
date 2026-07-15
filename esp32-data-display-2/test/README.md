# Display monitoring host test

## POSIX shell (`sh`)

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -Iesp32-data-display-2/main \
  esp32-data-display-2/main/monitoring.c \
  esp32-data-display-2/test/test_monitoring.c \
  -lm -o monitoring_test
./monitoring_test
```

## Windows PowerShell

```powershell
gcc -std=c11 -Wall -Wextra -Werror `
  -Iesp32-data-display-2/main `
  esp32-data-display-2/main/monitoring.c `
  esp32-data-display-2/test/test_monitoring.c `
  -lm -o monitoring_test.exe
.\monitoring_test.exe
```
