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
