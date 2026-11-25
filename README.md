# Elecraft AUXBUS - Reverse Engineering

English | [Polski](README.pl.md)

> AUXBUS is a proprietary protocol of Elecraft. This documentation is
> based on the analysis of sniffed communication between the K4, K3,
> KPA1500, KPA500 and KRC2, and on publicly available Elecraft
> documentation. It is not official documentation and may contain
> errors.

AUXBUS is a single-wire bus over which Elecraft radios (K4, K3) exchange
information with accessories: the KPA1500 and KPA500 amplifiers, the
KRC2 switch and transverters?. The radio reports the band and frequency,
and the amplifier reports its state, faults, band buttons and requests
for a tuning carrier.

## Repository contents

| Path | Contents |
|---|---|
| [`AUXBUS.md`](AUXBUS.md) | protocol specification: physical layer, K\* and ACC frames, commands, timing, implementation notes |
| [`saleae_lla/`](saleae_lla) | Low Level Analyzer (C++) for Saleae Logic 2: “Elecraft AUXBUS LLA” |
| [`saleae_hla/`](saleae_hla) | High Level Analyzer (Python) for Saleae Logic 2: “Elecraft AUXBUS HLA” |
| [`auxbus_tool/`](auxbus_tool) | AUXBUS frame monitor and transmitter for Arduino Uno |
| [`examples/`](examples) | communication captures in Saleae Logic 2 format (`.sal`) |

---

## Saleae Logic 2

The analyser works in two layers:

- **LLA** splits the waveform into frames and samples the bits the way
  the frame's receiver does. K\* frames are sampled in the middle of the
  476.1 µs bits, ACC frames the way the K4 receives them, and the
  response byte the way the K4 reads it.
- **HLA** assembles the bits into frames and describes them: source,
  type, fields, and finally the interpretation.

### Building the LLA

You need CMake ≥ 3.13, a C++17 compiler and network access: on the first
configuration CMake downloads the
[Saleae AnalyzerSDK](https://github.com/saleae/AnalyzerSDK).

```bash
cd saleae_lla
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Resulting library:

| System | File |
|---|---|
| macOS | `build/libauxbus_lla.dylib` |
| Linux | `build/libauxbus_lla.so` |
| Windows | `build/Release/auxbus_lla.dll` |

### Installing in Logic 2

1. **LLA:** in *Preferences → Custom Low Level Analyzers* select the
   directory with the library (e.g. `saleae_lla/build`) and restart
   Logic 2. Logic 2 must be restarted after every library update.
2. **HLA:** in the *Extensions* panel choose *Load Existing Extension…*
   and select `saleae_hla/extension.json`.

### Usage

1. Connect an analyser channel to the AUXBUS line and the analyser
   ground to the radio ground. A 1 MS/s sample rate is enough.
2. Add the **Elecraft AUXBUS LLA** analyser:
   - *AUXBUS*: the channel with the AUXBUS line,
   - *Amplifier LOW threshold [us]*: a LOW at least this long means an
     ACC frame. Default 10 000 µs; K\* frames have a LOW of ≈ 1900 µs,
     ACC frames ≈ 80 000 µs.
3. Add the **Elecraft AUXBUS HLA** analyser and select the LLA as its
   input.

On the graph the LLA shows:

- `PRE`: the preamble, from the falling edge to the first sample. The up
  arrow marks the rising edge `t_r` from which the samples are counted.
- bits: windows around the sampling instants, with a `1` or `0` marker at
  the sample. A red dot means an edge closer than 10 % of the bit period
  to the sample, i.e. an uncertain reading.
- `IDLE`: idle until the next frame.

If the line is still LOW after the last sample, the LLA starts a new
frame there and marks it as a collision. A LOW right after the response
window is marked as a response tail (see
[AUXBUS.md §2.5](AUXBUS.md#25-response)).

Example HLA output:

```
K* BAND cmd=0x20 data=0x02 response=0xFF  band 40m
K* XVTR cmd=0x21 data=0x00 response=0xFF  XVTR off (HF band)
K* FREQ value=0x437828  7105 kHz, checksum OK
K* STATUS cmd=0x34 data=0x01 response=0xA1  read KRC2 status char; K4 stores response: AX; -> AXA1;
KPA*500 STATE byte=0x37  on (KPA1500)
KPA*500 FAULT byte=0x29  fault 9: SWR too high
? ? byte=0x7F  unknown, ignored by K4, OPER  ! response tail after K4FP window, K4FP reads it as an amp frame
```

- Source: `K*` is the radio, `KPA*500` is an ACC frame, `?` is a response
  tail received by the K4 as an ACC frame.
- A CMD frame always has a `response` field, as the K4 would read it.
  `0xFF` means no response.
- Warnings, e.g. a collision, a preamble LOW outside 1.6–2.2 ms or an
  edge near a sample, are at the end of the line after `!`.

### Example captures

Files from the [`examples/`](examples) directory are opened in Logic 2
with *File → Open Capture*. Then add the analysers as described above.

---

## Arduino AUXBUS

The [`auxbus_tool`](auxbus_tool) sketch turns an Arduino Uno into an
AUXBUS monitor and transmitter:

- it receives K\* and ACC frames and prints them with their
  interpretation,
- `AXccdd;` sends a K\* CMD frame and immediately prints the response,
  read the same way the K4 reads it,
- `AAdd;` sends an ACC frame,
- `PU1;` enables the AUXBUS pull-up to 1.

### Wiring

| Arduino | AUXBUS |
|---|---|
| D2 | AUXBUS line |
| GND | ground |

D2 works as an open-drain output: `0` is output LOW, `1` is a
high-impedance input. The line is never driven HIGH. The pull-up is off
after start-up. If there is no other pull-up on the bus, e.g. with a
KRC2 alone and no radio, enable the internal pull-up with `PU1;`.

### Uploading

Open `auxbus_tool/auxbus_tool.ino` in the Arduino IDE and upload it to an
Arduino Uno, or use arduino-cli:

```bash
arduino-cli compile --fqbn arduino:avr:uno auxbus_tool
arduino-cli upload  --fqbn arduino:avr:uno -p <port> auxbus_tool
```

Serial port: 115200 bit/s. A command ends with `;`, Enter or both.
Commands are case-insensitive.

### Commands

| Command | Action |
|---|---|
| `AXccdd;` | sends a K\* CMD frame (cc = command, dd = data, hex), then reads the response in the K4 window and prints the frame with it |
| `AX;` | last response, answer `AXnn;` |
| `AAdd;` | sends an ACC frame with byte dd = `00`…`7F` (bit 6 = OPER) |
| `PU1;` / `PU0;` | AUXBUS pull-up to 1 (D2 pull-up) on / off |
| `PU;` | pull-up state, answer `PU0;` or `PU1;` |
| `?;` | help and line state |

The program echoes every typed character immediately; Backspace deletes
the last character. If a frame arrives while a command is being typed,
the typed text is shown again below it. An invalid command gives `?;`.
Before transmitting, the program waits until the bus has been idle for
15 ms. If it does not become idle within 2 s, the frame is not sent.

Example ACC bytes (full list in [AUXBUS.md §6](AUXBUS.md#6-acc-commands)):

| Command | Frame |
|---|---|
| `AA03;` | band button 40 m, STBY |
| `AA43;` | band button 40 m, OPER |
| `AA29;` | fault 9: SWR too high |
| `AA37;` | on (KPA1500) |
| `AA35;` / `AA36;` | KEY START / KEY STOP |

The K4 receives K\* frames sent with `AX` like any LOW on the bus, i.e.
as an ACC frame. The command is mainly useful with accessories and no
radio on the bus (KRC2, KPA1500).

### Output

Each line starts with the direction: `RX` is a received frame, `TX` a
transmitted one. It is followed by the source (`K*` or `ACC`), type,
fields and interpretation. Warnings are at the end after `!`.

```
RX K* BAND cmd=0x20 data=0x02 response=0xFF  band 40m
RX K* XVTR cmd=0x21 data=0x00 response=0xFF  XVTR off (HF band)
RX K* FREQ value=0x437828  7105 kHz, checksum OK
RX ACC STATE byte=0x37  on (KPA1500, RADIO TYPE = K4)
TX K* STATUS cmd=0x34 data=0x01 response=0xA1  KRC2 status char
RX ACC ? byte=0x7F  unknown, ignored by K4, OPER  ! response tail, K4 reads it as an ACC frame
```

Warnings:

| Warning | Meaning |
|---|---|
| `response tail` | LOW right after the response window, received by the K4 as an ACC frame (AUXBUS.md §2.5) |
| `collision` | the line was still LOW after the last sample: a new frame started during the previous one |
| `preamble LOW N us` | K\* preamble LOW outside 1.6–2.2 ms |
| `edge near sample N` | an edge closer than 10 % of a bit to sample N, uncertain reading |
| `AUXBUS held LOW for more than 1 s` | the line is held LOW, the K4 cannot transmit |

Example: the amplifier reports the 40 m band button, and the radio
responds by changing band and sending BAND, XVTR and FREQ frames.

```
AA03;
TX ACC BAND byte=0x03  band button 40m
RX K* BAND cmd=0x20 data=0x02 response=0xFF  band 40m
RX K* XVTR cmd=0x21 data=0x00 response=0xFF  XVTR off (HF band)
RX K* FREQ value=0x437828  7105 kHz, checksum OK
```
