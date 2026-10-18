# Parameters and saved state

Parameter IDs are stable within a state schema. Every parameter has version hint 1. "Configuration" parameters re-prepare the chip when they change: the plugin is silent for a moment while the message thread rebuilds it, and the latency may change. The other parameters act at the start of the next audio block.

## Shared by both plugins

| ID              | Name                  | Values                                         | Default       | Kind          | Library mapping                                     |
| --------------- | --------------------- | ---------------------------------------------- | ------------- | ------------- | --------------------------------------------------- |
| `deemphasis`    | De-emphasis           | Off, 32 kHz, 44.1 kHz, 48 kHz                  | Off           | pin event     | `ControlEvent::deemphasis` (DEM pins)               |
| `softMute`      | Soft mute             | off, on                                        | off           | pin event     | `ControlEvent::softMute` (SMUTE pin, 1024 frames)   |
| `profile`       | Profile               | Specification, NominalAnalog                   | Specification | configuration | `DacConfig::profile`                                |
| `noise`         | Noise                 | off, on                                        | on            | configuration | `AnalogOptions::noise` (NominalAnalog only)         |
| `distortion`    | Distortion            | off, on                                        | on            | configuration | `AnalogOptions::distortion` (NominalAnalog only)    |
| `mismatch`      | Channel mismatch      | off, on                                        | on            | configuration | `AnalogOptions::channelMismatch` (NominalAnalog only) |
| `crosstalk`     | Crosstalk             | off, on                                        | on            | configuration | `AnalogOptions::crosstalk` (NominalAnalog only)     |
| `dcOffset`      | DC offset             | off, on                                        | off           | configuration | `AnalogOptions::dcOffset` (NominalAnalog, DAC only) |
| `outputCircuit` | Fig.11 output circuit | off, on                                        | off           | configuration | `exampleOutputCircuit`                              |
| `outputGain`    | Output gain           | -24 to +12 dB, 0.1 dB steps                    | 0 dB          | host          | after the chip; smoothed over 20 ms                 |

The analog switches only matter with the NominalAnalog profile; with Specification the chip is clean whatever they say.

## AK4522 DAC (effect)

| ID                | Name             | Values                                      | Default  | Kind          | Library mapping                     |
| ----------------- | ---------------- | ------------------------------------------- | -------- | ------------- | ----------------------------------- |
| `dacRate`         | DAC rate         | 16, 22.05, 24, 32, 44.1, 48 kHz             | 44.1 kHz | configuration | `DacConfig::dacRate`                |
| `inputResolution` | Input resolution | 20-bit, 16-bit                              | 20-bit   | configuration | `HostConfig::inputResolution`       |
| `inputDither`     | Input dither     | off, on                                     | off      | configuration | `HostConfig::inputDither` (TPDF)    |
| `path`            | Path             | DAC, DAC into ADC, ADC                      | DAC      | configuration | `HostConfig::path`                  |
| `mix`             | Mix              | 0 to 100 %                                  | 100 %    | host          | wet against the latency-aligned dry |
| `delta`           | Delta listen     | off, on                                     | off      | host          | processed minus aligned dry         |
| `bypass`          | Bypass           | off, on                                     | off      | host          | the host's bypass parameter         |

## AK4522 Synth (instrument)

| ID             | Name          | Values                                                   | Default | Kind          | Library mapping                  |
| -------------- | ------------- | -------------------------------------------------------- | ------- | ------------- | -------------------------------- |
| `clock`        | Source clock  | 32 kHz, 44.1 kHz, 48 kHz, 112 MHz / 2304 (48.611 kHz)    | 48 kHz  | configuration | `DacStreamConfig::dacRate`; the last uses the extended range |
| `sampleFormat` | Sample format | 20-bit, 16-bit                                           | 20-bit  | configuration | `DacSampleFormat::float64` or `int16` |
| `waveform`     | Waveform      | Sine, Saw, Square                                        | Saw     | host          | the test synth                   |
| `attack`       | Attack        | 1 to 2000 ms                                             | 5 ms    | host          | the test synth                   |
| `release`      | Release       | 5 to 5000 ms                                             | 200 ms  | host          | the test synth                   |

## What each control does

The editor groups the controls under the bold headings below, each plugin's own groups first and the shared ones after, and shows each description below as the control's tooltip. Each description ends with where the behavior comes from:

- _AK4522 behavior, from the ak4522 library._
- _From the ak4522 library; not a chip control._
- _Plugin behavior; not part of the AK4522._

The descriptions are kept in `Source/common/Descriptions.h`; the tests check that this section uses the same wording. Chip and library descriptions are one line; the linked library documentation, at the pinned tag, is the full account. The groups do not change parameter IDs or saved state.

### AK4522 DAC only

**Converter.**

- **DAC rate** (`dacRate`): The chip's sample rate. Host audio is converted to it, passes through the chip, and is converted back to the host rate. _AK4522 behavior, from the ak4522 library._ Library: [Host-rate adaptation](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#host-rate-adaptation).
- **Input resolution** (`inputResolution`): The word length host audio is quantized to before the chip: 20-bit, the chip's full input, or 16-bit. _From the ak4522 library; not a chip control._ Library: [PCM mapping and quantization](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#pcm-mapping-and-quantization).
- **Input dither** (`inputDither`): Adds TPDF dither before host audio is quantized to the chip's input. _From the ak4522 library; not a chip control._ Library: [Host-rate adaptation](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#host-rate-adaptation).
- **Path** (`path`): What host audio passes through: the DAC, the DAC's analog output into the ADC, or the ADC alone. _From the ak4522 library; not a chip control._ Library: [ADC paths in HostProcessor](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/api.md#adc-paths-in-hostprocessor).

### AK4522 Synth only

**Source.**

- **Source clock** (`clock`): The rate at which the test synth's samples reach the DAC. 112 MHz / 2304 is above the datasheet's 48 kHz limit and uses the library's extended range. _AK4522 behavior, from the ak4522 library._ Library: [DacStream](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#dacstream), [Extended DAC-rate range](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#extended-dac-rate-range).
- **Sample format** (`sampleFormat`): The word length of the test synth's samples: 20-bit, the chip's full input, or 16-bit. _From the ak4522 library; not a chip control._ Library: [DacStream](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#dacstream).

**Test synth (not part of the chip).**

- **Waveform** (`waveform`): The test synth's oscillator shape. _Plugin behavior; not part of the AK4522._
- **Attack** (`attack`): The test synth's fade-in time at the start of each note. _Plugin behavior; not part of the AK4522._
- **Release** (`release`): The test synth's fade-out time after each note ends. _Plugin behavior; not part of the AK4522._

### Both plugins

**Chip pins.**

- **De-emphasis** (`deemphasis`): The chip's 50/15 us de-emphasis filter for 32, 44.1, or 48 kHz material, selected as the DEM pins would. _AK4522 behavior, from the ak4522 library._ Library: [De-emphasis](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#de-emphasis).
- **Soft mute** (`softMute`): Ramps the output to silence over 1024 DAC frames, and back over the same time when released, as the SMUTE pin does. _AK4522 behavior, from the ak4522 library._ Library: [Soft mute](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#soft-mute).

**Model.**

- **Profile** (`profile`): Specification is the clean, deterministic model of the datasheet response. NominalAnalog adds the synthetic imperfections switched below. _From the ak4522 library; not a chip control._ Library: [Profiles and options](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/integration.md#profiles-and-options).

**Analog imperfections (NominalAnalog only).**

- **Noise** (`noise`): Synthetic noise at the datasheet's typical level. NominalAnalog only. While it plays, the plugin's tail is infinite. _From the ak4522 library; not a chip control._ Library: [NominalAnalog noise](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#nominalanalog-noise), [profile report](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/analog_profile.md).
- **Distortion** (`distortion`): Synthetic 2nd and 3rd harmonic distortion at the datasheet's typical level. NominalAnalog only. _From the ak4522 library; not a chip control._ Library: [NominalAnalog output stage](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#nominalanalog-output-stage).
- **Channel mismatch** (`mismatch`): A fixed 0.2 dB gain difference between the channels. NominalAnalog only. _From the ak4522 library; not a chip control._ Library: [NominalAnalog output stage](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#nominalanalog-output-stage).
- **Crosstalk** (`crosstalk`): Leakage between the channels that rises with frequency, like capacitive coupling. NominalAnalog only. _From the ak4522 library; not a chip control._ Library: [NominalAnalog output stage](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#nominalanalog-output-stage).
- **DC offset** (`dcOffset`): A small DC offset on the DAC output. NominalAnalog only; the ADC and the Fig.11 circuit remove it. _From the ak4522 library; not a chip control._ Library: [NominalAnalog output stage](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#nominalanalog-output-stage).

**Board circuit (not part of the chip).**

- **Fig.11 output circuit** (`outputCircuit`): The datasheet's Fig.11 board circuit after the chip: two 0.589 Hz high-pass sections around a gain of -1.8 (+5.1 dB). Not part of the chip. _From the ak4522 library; not a chip control._ Library: [Example output circuit](https://github.com/remix1201/ak4522/blob/v0.1.0/docs/features.md#example-output-circuit-fig11).

**Plugin (not part of the chip).** Mix, Delta listen, and Bypass are in the AK4522 DAC only.

- **Output gain** (`outputGain`): Level after the chip and the Fig.11 circuit, smoothed over 20 ms. _Plugin behavior; not part of the AK4522._
- **Mix** (`mix`): Balance of processed and dry signal. The dry signal is delayed by the reported latency so the two line up. _Plugin behavior; not part of the AK4522._
- **Delta listen** (`delta`): Plays the processed signal minus the aligned dry signal: only what the chip changes. _Plugin behavior; not part of the AK4522._
- **Bypass** (`bypass`): Passes the input through, delayed by the reported latency, so switching does not shift the timing. _Plugin behavior; not part of the AK4522._

## Timing

- **Pin events:** a change of `deemphasis` or `softMute` is sent at the start of the block in which the host delivers it. JUCE gives one value per block, so a change cannot be placed inside a block; with small host buffers the error is small. In the synth, the event acts on the first DAC frame of the next source request.
- **MIDI (synth):** each event at host frame t acts on DAC frame ceil(t x dacRate / hostRate), computed exactly from the rational clock. A note therefore starts on the same source frame for any host block size, which the tests check bitwise.

## Latency and tail

- **Latency:** the reported latency is the library's `LatencyInfo::reportedHostFrames` for the configuration. The library aligns its output to that whole number of host frames, and the tests check the emitted audio against it on every path.
- **Bypass and mix:** both use the input delayed by exactly the reported latency. Bypass crossfades over 20 ms; the chip keeps running while bypassed.
- **Tail:** infinite while the NominalAnalog profile plays noise. Otherwise it is the latency plus a settling allowance for the slowest filter on the path to fall 120 dB: 0.1 s on the DAC path, 3 s on the ADC paths (the ADC's 0.9 Hz high-pass filter), and 4 s with the Fig.11 circuit (its 0.59 Hz coupling). The synth adds its release time.

## Saved state

The state is the parameter tree as XML (root `AK4522DemoState`) with three attributes:

| Attribute   | Meaning                                                         |
| ----------- | --------------------------------------------------------------- |
| `schema`    | state format version, currently 1                               |
| `plugin`    | `effect` or `synth`; each plugin rejects the other's state      |
| `noiseSeed` | the NominalAnalog noise seed, decimal; kept across save and load |

State with another root, another plugin kind, or another schema is rejected: the plugin keeps its current settings and the status line says why. There is no migration during alpha.
