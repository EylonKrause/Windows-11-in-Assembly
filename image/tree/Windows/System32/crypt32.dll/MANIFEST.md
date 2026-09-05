# crypt32.dll — reimplemented exports (16)

| export | speedup | source |
|---|---|---|
| `CryptBinaryToStringA` | 150x | [changes/090-cryptbinarytostring-hexfmt](../../../../../changes/090-cryptbinarytostring-hexfmt/) |
| `CryptBinaryToStringA` | 16.1x | [changes/092-cryptbinarytostring-base64header](../../../../../changes/092-cryptbinarytostring-base64header/) |
| `CryptBinaryToStringA` | 24.5x | [changes/081-cryptbinarytostring-base64](../../../../../changes/081-cryptbinarytostring-base64/) |
| `CryptBinaryToStringA` | 920x | [changes/085-cryptbinarytostring-hexraw](../../../../../changes/085-cryptbinarytostring-hexraw/) |
| `CryptBinaryToStringW` | 133x | [changes/087-cryptbinarytostringw-hexraw](../../../../../changes/087-cryptbinarytostringw-hexraw/) |
| `CryptBinaryToStringW` | 57x | [changes/093-cryptbinarytostringw-base64header](../../../../../changes/093-cryptbinarytostringw-base64header/) |
| `CryptBinaryToStringW` | 77x | [changes/091-cryptbinarytostringw-hexfmt](../../../../../changes/091-cryptbinarytostringw-hexfmt/) |
| `CryptBinaryToStringW` | 8.9x | [changes/083-cryptbinarytostringw-base64](../../../../../changes/083-cryptbinarytostringw-base64/) |
| `CryptStringToBinaryA` | 11.0x | [changes/104-cryptstringtobinary-base64header](../../../../../changes/104-cryptstringtobinary-base64header/) |
| `CryptStringToBinaryA` | 259x | [changes/086-cryptstringtobinary-hexraw](../../../../../changes/086-cryptstringtobinary-hexraw/) |
| `CryptStringToBinaryA` | 36.3x | [changes/082-cryptstringtobinary-base64](../../../../../changes/082-cryptstringtobinary-base64/) |
| `CryptStringToBinaryA` | 5.3x | [changes/106-cryptstringtobinary-base64any](../../../../../changes/106-cryptstringtobinary-base64any/) |
| `CryptStringToBinaryW` | 11.7x | [changes/105-cryptstringtobinaryw-base64header](../../../../../changes/105-cryptstringtobinaryw-base64header/) |
| `CryptStringToBinaryW` | 248x | [changes/088-cryptstringtobinaryw-hexraw](../../../../../changes/088-cryptstringtobinaryw-hexraw/) |
| `CryptStringToBinaryW` | 38.7x | [changes/084-cryptstringtobinaryw-base64](../../../../../changes/084-cryptstringtobinaryw-base64/) |
| `CryptStringToBinaryW` | 5.8x | [changes/107-cryptstringtobinaryw-base64any](../../../../../changes/107-cryptstringtobinaryw-base64any/) |
