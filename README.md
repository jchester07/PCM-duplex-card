# PCM5102A DAC + PCM1808 ADC full duplex on Raspberry Pi (I2S)

Linux ASoC drivers and a device-tree overlay that run a **PCM5102A DAC** and a
**PCM1808 ADC** together on the Raspberry Pi's single I2S port, with the
**PCM1808 as the clock master** (driven by a 24.576 MHz oscillator) and the Pi
as a clock consumer. Fixed 48 kHz, stereo, 32-bit slots.

Tested on a Raspberry Pi 4B, kernel `6.18.50+rpt-rpi-v8` (Raspberry Pi OS based,
Zynthian), with JACK at 48 kHz.

## Why a custom card is needed

The Pi has one I2S controller, and ASoC allows only one sound card to own it.
Using one overlay per chip (e.g. a stock DAC overlay plus an ADC overlay) makes
the two cards fight over the controller, and stock overlays make the Pi the
clock master, which is wrong when the ADC generates the clocks.

This project solves it with **one card, one DAI link, two codecs**:

- the PCM5102A is playback-only,
- the PCM1808 is capture-only,
- ASoC routes each direction to the right chip,
- the PCM1808 provides BCK and LRCK, and the Pi and the PCM5102A consume them.

## Contents

| File | Purpose |
|---|---|
| `pcm1808-master.c` | Capture-only codec driver for a PCM1808 strapped as I2S master. The chip has no control interface, so this only describes the DAI to ASoC and limits it to 48 kHz. |
| `rpi-duplex-card.c` | Machine driver: ties the Pi I2S, the PCM5102A and the PCM1808 into one card named `PiDuplex`. Locks rate to 48 kHz and channels to 2, forces the Pi I2S to clock consumer, and sets the BCK ratio to 64. |
| `pcm5102a-pcm1808-duplex-overlay.dts` | Device-tree overlay: enables I2S, defines both codec nodes and the sound card. |
| `Makefile` | Out-of-tree build for both modules and the overlay. |

The PCM5102A uses the mainline `snd-soc-pcm5102a` driver (`ti,pcm5102a`).

## Hardware

Pi 4B I2S pins (physical pin numbers in brackets):

| Signal | Pi pin | Connects to |
|---|---|---|
| BCK | GPIO18 (12) | PCM1808 BCK, PCM5102A BCK |
| LRCK | GPIO19 (35) | PCM1808 LRCK, PCM5102A LRCK |
| DIN | GPIO20 (38) | PCM1808 DOUT |
| DOUT | GPIO21 (40) | PCM5102A DIN |

**PCM1808**

- SCKI: 24.576 MHz oscillator (3.3 V logic)
- MD1 = 0, MD0 = 1: master mode, 512fs (24.576 MHz / 512 = 48 kHz)
- FMT = 0: I2S
- PDWN = high (clocks stop if it is low)
- VCC (analog) 5 V and VDD (digital) 3.3 V, common ground with the Pi
- Analog inputs need AC coupling; full scale is about 2 Vpp, so a guitar
  pickup needs a preamp

**PCM5102A**

- SCK tied to GND (internal PLL derives its clock from BCK)
- FMT, FLT, DEMP low and XSMT high, as usual for most breakout boards

Check the strap levels against the datasheet and your breakout board.
Keep the BCK, LRCK and data wires short: long jumper wires add noise and
distortion, especially on the ADC side.

## Build and install (on the Pi)

```
sudo apt install raspberrypi-kernel-headers device-tree-compiler
make
make install
```

Add to `/boot/firmware/config.txt`:

```
dtoverlay=pcm5102a-pcm1808-duplex
dtparam=audio=off
```

Reboot. `aplay -l` and `arecord -l` should list a card named **PiDuplex**.

## Test

```
arecord -D hw:PiDuplex -f S32_LE -r 48000 -c 2 -vv /dev/null     # live level meter
speaker-test -D hw:PiDuplex -c 2 -r 48000 -F S32_LE -t sine
arecord -D hw:PiDuplex -f S32_LE -r 48000 -c 2 | aplay -D hw:PiDuplex -f S32_LE -r 48000 -c 2
```

JACK example:

```
jackd -R -P70 -d alsa -d hw:PiDuplex -r 48000 -p 256 -n 2
```

The codec streams advertise only `S32_LE` and `S24_LE`, so JACK runs the card
in 32-bit. If you pass the ALSA backend's 16-bit option (`-S` / `--shorts`,
placed after `-d alsa`), JACK tries 16-bit first, fails to open it and falls
back to 32-bit. (A `-S` placed before `-d alsa` is a different, server-level
option and does not affect the sample format.)

## Design notes and limitations

- **Fixed 48 kHz.** The PCM1808 master clocks are derived from the 24.576 MHz
  oscillator and cannot change, so the card locks the rate and channel count.
- **Frame length.** As a consumer, the Pi I2S cannot derive the frame length
  from its own divider, so the machine driver sets a BCK ratio of 64
  (PCM1808 master mode gives 64 BCK per frame).
- **Clock roles.** The machine driver sets the Pi I2S CPU DAI to clock consumer
  explicitly in `hw_params`. The ASoC core was observed passing consumer flags
  to the second codec on the link, so the codec driver accepts the I2S format
  but does not reject the provider or polarity bits: the master/slave mode and
  format of the PCM1808 are fixed by its MD0, MD1 and FMT pins.
- **Kernel API names.** The code uses the kernel 6.10+ names such as
  `snd_soc_rtd_to_cpu()`. On 5.18 to 6.9 replace it with `asoc_rtd_to_cpu()`.
- **Overlay labels.** The overlay uses `i2s_clk_producer`. If your kernel lacks
  that label, check `ls /sys/firmware/devicetree/base/__symbols__ | grep i2s`
  and adjust (older kernels use `i2s`; `i2s_clk_consumer` also exists on recent
  kernels).

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `ASoC error (-22): at snd_soc_dai_set_fmt()` and card registration fails | A codec driver rejecting the DAI format flags |
| `bcm2835-i2s: FIFO clear timed out: no PCM clock` | The Pi is a clock consumer but the PCM1808 is not producing BCK/LRCK: check SCKI (24.576 MHz), PDWN, VCC and VDD, and the MD0/MD1 straps |
| `arecord: read error: Input/output error` or JACK poll timeouts | Same as above: no clock reaching the Pi |
| Playback works but capture is silent (meter at 0%) | PCM1808 DOUT not reaching GPIO20 (bad wire or wrong pin); test by jumpering GPIO21 to GPIO20 with the ADC DOUT disconnected |
| Capture reads loud random values | Format mismatch: check that FMT is low (I2S) |
| Card shows as busy (`0/1` subdevices) | Something else (JACK, a service) has the card open |

## License

GPL-2.0 (the source files carry an SPDX header and the modules declare
`MODULE_LICENSE("GPL")`). Add a `LICENSE` file with the GPL-2.0 text to the
repository.
