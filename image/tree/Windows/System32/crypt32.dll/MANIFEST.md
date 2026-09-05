# crypt32.dll — reimplemented exports (6)

| export | speedup | source |
|---|---|---|
| `CryptBinaryToStringA` | 24.5x | [changes/081-cryptbinarytostring-base64](../../../../../changes/081-cryptbinarytostring-base64/) |
| `CryptBinaryToStringA` | 920x | [changes/085-cryptbinarytostring-hexraw](../../../../../changes/085-cryptbinarytostring-hexraw/) |
| `CryptBinaryToStringW` | 8.9x | [changes/083-cryptbinarytostringw-base64](../../../../../changes/083-cryptbinarytostringw-base64/) |
| `CryptStringToBinaryA` | 259x | [changes/086-cryptstringtobinary-hexraw](../../../../../changes/086-cryptstringtobinary-hexraw/) |
| `CryptStringToBinaryA` | 36.3x | [changes/082-cryptstringtobinary-base64](../../../../../changes/082-cryptstringtobinary-base64/) |
| `CryptStringToBinaryW` | 38.7x | [changes/084-cryptstringtobinaryw-base64](../../../../../changes/084-cryptstringtobinaryw-base64/) |
