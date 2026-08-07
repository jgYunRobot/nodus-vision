# PCD1 version 2 binary point-cloud contract

PCD1 v2 is a little-endian snapshot format. Coordinates are `float32` meters in the camera optical
frame. RGB values are unsigned bytes. Dynamic robot/world transforms are not embedded.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII `PCD1` |
| 4 | 4 | `uint32` version, exactly `2` |
| 8 | 8 | `uint64` frame number |
| 16 | 8 | `int64` monotonic capture timestamp nanoseconds |
| 24 | 4 | `uint32` source width |
| 28 | 4 | `uint32` source height |
| 32 | 4 | `uint32` requested pixel stride |
| 36 | 4 | `uint32` actual pixel stride |
| 40 | 4 | `uint32` point count `N` |
| 44 | 4 | reserved, exactly zero |
| 48 | 16 | `float32` intrinsics `fx`, `fy`, `ppx`, `ppy` |
| 64 | 48 | `float32[12]` identity camera-to-frame matrix3x4 |
| 112 | `12*N` | packed `float32` optical XYZ array |
| `112+12*N` | `3*N` | packed `uint8` RGB array |

The exact payload length is `112 + 15*N`. Capture generation, Unix epoch capture timestamp, sensor
frame and calibration identity are carried by the corresponding `X-Nodus-*` HTTP headers. Readers
must reject unknown versions, nonzero reserved fields, truncated/trailing bytes, invalid bounds and
non-finite point coordinates.
