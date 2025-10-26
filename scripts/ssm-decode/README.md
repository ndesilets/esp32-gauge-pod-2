# ssm-decode

reads in CANHacker .trc files captured from COBB Accessport traffic and outputs memory addresses and their respective values. decodes them if they're present in `known-addresses.yaml`, otherwise prints them as-is.

if you want to know how ISO-TP and SSM works this seems to be close enough, but probably wouldn't trust my life with it lol

requirements:
- python >= 3.10

## usage

provide the path to a CANHacker `.trc` file w/ `--trc` (or `-t`) flag.

```powershell
python main.py --trc "mytrace.trc"
```