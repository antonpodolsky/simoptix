# Performance overlay: image layers

`perf.html` is the source of the performance overlay's three image layers. Open it with
`#chrono-base`, `#chrono-glass` or `#oled-base` to see each, add `-preview` to see it over a backdrop,
and add `-text` for sample readings. The OBS plugin embeds the PNGs (`overlays\performance\performance.rc`)
and draws the live parts from the tables below (`overlays\performance\performance_theme.cpp`).

| File | What it holds |
|---|---|
| `perf.html` | The layers, drawn in HTML and CSS from the SimOptix Overlays design system's tokens |
| `perf_chrono_base.png` | Chrono: the bezel with its shadow, the sunken face, the dividers and the empty GPU HR and CPU HR tracks |
| `perf_chrono_glass.png` | Chrono: the glass band over the face's top half, 13 % white at most. The plugin draws it **last**, over the numbers |
| `perf_oled_base.png` | OLED: the black display with its 1 px edge, dark halo and shadow, the dividers and the empty segmented GPU HR and CPU HR tracks. OLED has no glass |

Each is 624 × 328 px, the 312 × 164 box at 2×, transparent outside the overlay and its shadow.
Rendered by Edge at 2× from `perf.html` (the dev skill, "4a. Take a design into the plugin"), the three
match the design system's PNGs pixel for pixel. Below, sizes are design px: canvas px on a 1080-pixel
canvas at the design's size. The plugin shows the box at two thirds of that (208 × 109 on 1080p).

## Fixed geometry (in the layers)

| Part | Where |
|---|---|
| Box | 312 × 164. The bezel 232 × 68 at (40, 40), radius 14, with 40 px of room left, right and above and 56 below for the shadow |
| Face | 220 × 56 at (46, 46), radius 9, in three equal cells: FPS, GPU HR, CPU HR |
| Dividers | 0.8 wide at the cell edges, 10 from the face's top and bottom. White at 6 % (Chrono), 10 % (OLED) |
| Tracks | GPU HR and CPU HR only: from the cell's x + 10 to its end − 10, at y 91 (the face's 45), 3 high, flat ends. White at 7 % (Chrono), 10 % in segments of 5 on and 2 off from the left end (OLED) |

## Drawn live (between the layers)

Each text starts at its cell's x + 10, placed by its pen (its advance's left end), as the design's text
boxes are. Its ink's middle is on the line given, measured on `perf.html#chrono-base-text` rendered at
2×.

| Part | Spec |
|---|---|
| Label | FPS, GPU HR, CPU HR. Bahnschrift Regular 9 px, letter spacing 0.1 em. White at 85 % in both themes. Ink 55.5 to 62.0, middle 58.75 |
| Reading | Bahnschrift SemiBold 20 px, white, tabular figures, whole numbers. The critical red when critical. Digits' ink 67.0 to 81.0, middle 74.0 |
| % | After GPU HR and CPU HR, 1 px after the reading's advance. Bahnschrift Regular 11 px, the label's colour. Ink 73.0 to 81.0, middle 77.0 |
| Bar fill | Over the track, the headroom over 100, clamped 0 to 1. Good `#3ddc84` from 10 %, warning `#ffb020` from 5 %, critical `#ff5a4e` below 5 %. OLED's is cut into the track's segments |

Below 9 px on screen, text is drawn one weight heavier, as the G-meter's is.
