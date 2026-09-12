# Independent measurement review

Reviewed 2026-09-12 from the two local on-device capture files, independently of the main report calculations. Network credentials, network names and scene text are omitted here.

## Comparison

The animated captures contain the same 12 scene commands, native 240 x 240 output, black background and four-second clock-colon pulse. Their request epochs differ because the runs occurred sequentially. Both report an 80 MHz CPU and Wi-Fi sleep mode 2 (modem sleep). Baseline and final image hashes match the independently validated binaries in `power-source-review.md`.

One animated capture per variant lasted 60,000 ms and 60,004 ms; one static capture per variant lasted 15,000 ms and 15,005 ms. The following rates use each actual elapsed duration, not an assumed common duration.

| Animated measure | Baseline | Efficient | Relative reduction |
| --- | ---: | ---: | ---: |
| Main-loop passes/second | 10,825.72 | 88.04 | 99.19% |
| Main-loop work wall time / elapsed time | 76.2986% | 10.7353% | 85.93% |
| Render wall time, ms/second | 127.151 | 98.200 | 22.77% |
| RGB565 pixel payload, bytes/second | 67,328.00 | 3,526.96 | 94.76% |
| Render calls/second | 10.000 | 9.799 | 2.01% |
| Mean render wall time, ms/call | 12.715 | 10.021 | 21.19% |

| Static measure | Baseline | Efficient | Relative reduction |
| --- | ---: | ---: | ---: |
| Main-loop passes/second | 12,451.07 | 97.77 | 99.21% |
| Main-loop work wall time / elapsed time | 73.0251% | 0.8964% | 98.77% |
| Render calls and transmitted pixels | 0 | 0 | No change |

The idle wait is responsible for the large reduction in repeated application polling. The static control already sent zero pixels before optimization, so zero static writes must not be claimed as a new improvement. Cropping colon transfers reduces the animated pixel payload; a host geometry check independently establishes a 12-pixel transfer width instead of 240 pixels, a 95% width reduction. The live reduction is 94.76%, including the captured clock behavior.

The final animation ran about 2% fewer render calls per second. Its mean render time still improved by 21.19%, so the lower total render time is not explained solely by fewer calls. With one run per variant, these are observations, not statistical confidence intervals or guarantees for all Wi-Fi/network conditions. There was no separate idle-only hardware run; the animated main-loop result combines the scheduling and renderer changes.

Animated periodic heap samples ended at 19,024 bytes free in the baseline and 18,936 bytes in the final image, a difference of -88 bytes. Renderer state grew by 8 bytes. Both captures lost the same 168 bytes between their first and last heap samples; these short captures alone cannot establish long-term leak freedom. Diagnostics were disabled and cleared after the captures, as confirmed by each final diagnostics record.

## Meaning and limits

Work and render times are wall-clock intervals. Render time is part of main-loop work and must not be added to it. SDK and interrupt work may occur during work or waiting; core scheduling outside the measured loop and diagnostic overhead are not a complete CPU accounting. The work percentage is therefore not hardware CPU utilization. RGB565 bytes count pixel payload, not SPI command bytes or total network traffic.

The experiment did not measure electrical current or temperature. CPU frequency, backlight behavior and modem-sleep policy were unchanged. The results demonstrate substantially less repeated application work and screen traffic, but do not quantify how much cooler the enclosure becomes. A temperature or current measurement is needed for that conclusion.
