# crypt32.dll — reimplemented exports (4)

| export | speedup | source |
|---|---|---|
| `CryptBinaryToStringA` | 24.5x | [changes/081-cryptbinarytostring-base64](../../../../../changes/081-cryptbinarytostring-base64/) |
| `CryptBinaryToStringW` | 8.9x | [changes/083-cryptbinarytostringw-base64](../../../../../changes/083-cryptbinarytostringw-base64/) |
| `CryptStringToBinaryA` | 36.3x | [changes/082-cryptstringtobinary-base64](../../../../../changes/082-cryptstringtobinary-base64/) |
| `CryptStringToBinaryW` | 38.7x | [changes/084-cryptstringtobinaryw-base64](../../../../../changes/084-cryptstringtobinaryw-base64/) |
