to sync `assets/` run `idf.py storage-flash`
default ppi was 130

176 bytes for monitored state

had to switch to compiling for space optimization when including uart because this is completely full of garbage apparently

esp-idf v5.5.2 for some reason breaks something with decoding jpegs even though the buffer contents seem correct, so stick with 5.5.1