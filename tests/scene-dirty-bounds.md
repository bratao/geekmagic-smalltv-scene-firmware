# Native scene invalidation regression

`scene-dirty-bounds.cpp` links the production portable rasterizer and fonts. Compile in a Visual Studio x64 developer prompt:

```bat
cl /nologo /std:c++17 /EHsc /Zi /fsanitize=address tests\scene-dirty-bounds.cpp src\scene\SceneCore.cpp src\scene\SceneFonts.cpp /Fe:tests\scene-dirty-bounds.exe /Fo:tests\ /Fd:tests\scene-dirty-bounds.pdb
tests\scene-dirty-bounds.exe
```

The test compares full pixel values at opposite pulse extrema across five fonts, five X positions, five Y positions and five clock values. All changed pixels must be inside the advertised colon rectangle; truncated clocks must fall back to conservative invalidation. It also exercises all 255 nonempty row masks across eight crop combinations to verify in-place packing preserves every transmitted pixel, including disjoint row runs.

MSVC 14.44 with AddressSanitizer: 625 clock cases and 2,040 packing cases passed. The example report clock at `(10,5)` uses a 12-pixel-wide colon, so its changed rows require 95% fewer pixel bytes than the previous 240-pixel-wide transfers. This is a deterministic transfer-width comparison, not a hardware CPU, electrical-power or temperature measurement.

An additional simulated display sequence compares 160 complete frames (9,216,000 pixels) against full reference rasterization. It covers an opaque rectangle overlapping the colon, a second independent pulse in the same bands, minute transitions including `09:59` to `10:00`, and scene replacement with a changed background. The sequence models invalidation, row hashes, in-place packing and display writes; it links the production rasterizer but does not compile the Arduino `SceneRenderer` wrapper itself. All cases passed under AddressSanitizer.

The harness does not emulate ESP8266 SPI, network interrupts, the main loop, or display hardware. Validate those separately on the device.
