# crypt32.dll — reimplemented exports (10)

| export | speedup | source |
|---|---|---|
| `CryptBinaryToStringA` | 150x | [changes/090-cryptbinarytostring-hexfmt](../../../../../changes/090-cryptbinarytostring-hexfmt/) |
| `CryptBinaryToStringA` | 24.5x | [changes/081-cryptbinarytostring-base64](../../../../../changes/081-cryptbinarytostring-base64/) |
| `CryptBinaryToStringA` | 920x | [changes/085-cryptbinarytostring-hexraw](../../../../../changes/085-cryptbinarytostring-hexraw/) |
| `CryptBinaryToStringW` | 133x | [changes/087-cryptbinarytostringw-hexraw](../../../../../changes/087-cryptbinarytostringw-hexraw/) |
| `CryptBinaryToStringW` | 77x | [changes/091-cryptbinarytostringw-hexfmt](../../../../../changes/091-cryptbinarytostringw-hexfmt/) |
| `CryptBinaryToStringW` | 8.9x | [changes/083-cryptbinarytostringw-base64](../../../../../changes/083-cryptbinarytostringw-base64/) |
| `CryptStringToBinaryA` | 259x | [changes/086-cryptstringtobinary-hexraw](../../../../../changes/086-cryptstringtobinary-hexraw/) |
| `CryptStringToBinaryA` | 36.3x | [changes/082-cryptstringtobinary-base64](../../../../../changes/082-cryptstringtobinary-base64/) |
| `CryptStringToBinaryW` | 248x | [changes/088-cryptstringtobinaryw-hexraw](../../../../../changes/088-cryptstringtobinaryw-hexraw/) |
| `CryptStringToBinaryW` | 38.7x | [changes/084-cryptstringtobinaryw-base64](../../../../../changes/084-cryptstringtobinaryw-base64/) |
