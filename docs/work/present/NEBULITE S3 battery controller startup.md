# NEBULITE S3 battery controller startup evidence

The historical NEBULITE WLED `nebulite3` source at commit
`e76b0b1bf4ec5076e3d929cc4278d373ebba59dd`, `wled00/wled.cpp`, in
`WLED::setup()`, configures GPIO10 as input. When GPIO10 reads HIGH, it
identifies a “Battery Controller Rev4” and sets GPIO21 HIGH (enable), then
GPIO14 HIGH (main enable). Otherwise it identifies a USB controller. The same
sequence is present in `autobuild` commit
`8c58252cc2f1c0edb1d52b39a625052ed86b8a8c`.

Source: https://github.com/AuraviaLTD/WLED/blob/e76b0b1bf4ec5076e3d929cc4278d373ebba59dd/wled00/wled.cpp

On 2026-10-30 the owner connected an ESP32-S3 NEBULITE battery controller over
native USB without a battery, holding the power button with a clamp. USB reports
Espressif USB JTAG/serial, serial `DC:54:75:CC:82:00`; passive serial capture
repeated `invalid header: 0xa5ff005a`. USB repeatedly re-enumerated. This does
not identify the board revision or prove that flash is empty. No flash or GPIO
operation was performed. A read-only chip-identification attempt could not
start because the serial device disappeared during re-enumeration.

`nebulite-s3` (src/helio/NebuliteS3Power.h) runs the same sequence before any
module starts, then holds both enables with `gpio_hold_en`. The pad hold lives
in the RTC domain: a software restart, a panic or a watchdog reset keeps it, so
an update's restart no longer switches the controller off. Customers switch it
off with the reset button, which pulls EN low; per the ESP32-S3 datasheet EN low
switches the chip off and stops its pads driving, so the main MOSFET still drops.
That is still to be seen on the controller, whose clamp the owner removed on
2026-11-05. A battery-free USB test cannot qualify ADC measurement or charging.

On 2026-10-31 the live EasyEDA project `Nebulite S3 V6 - Minimal` (project
`3aac7de93e7345909d80e266141f39de`, PCB
`c7022539de4d4a899bbf3cde5233e38c`) was opened read-only and its PCB
components and pads checked. The microphone is ICS-43434; SW2 connects EN to
GND (reset), SW1 connects BTN_SELECT to GND, and SW3 connects IO0 to GND
(boot). These three switches are on the same side, ordered SW2, SW1, SW3 by
increasing PCB X; USB1 lies toward increasing X. The live geometry and nets
agree with the archived PCB reference. This is the V6 Minimal design: its
archived netlist has no battery chargers, and its match to the owner's physical
L-shaped battery controller remains unconfirmed. Do not transfer the older
Rev4 self-hold pins to this board on the basis of the name alone.

The owner then indicated that Minimal is probably not the connected board.
The live project `Nebulite s3 v2 Top_2026-10-04_01-48-41` (project
`5a57abf336f24d5e958010553894a1f0`, PCB `4c6d574bd7320452`) is a closer
battery-controller candidate: its archived source has two TP4056 chargers;
the live PCB has ONOFF, SELECT and RST buttons, and ESP32 module GPIO14 is
`MAIN_EN`, GPIO21 is `ENABLE` and GPIO10 is tied to `+3V3`, consistent with the
old Rev4 startup code. This does not establish identity of the physical board.
The live PCB has no separate BOOT component among those three buttons: GPIO0
connects to module pad 4 and Q2 pad 3. Q2 also connects DTR, RTS and CHIP_PU;
CN2 has pad 1 RTS, 2 DTR, 3 UART0_RX, 4 UART0_TX, 5 +3V3 and 6 GND. Native
USB still appears as ESP USB JTAG/serial but currently repeatedly re-enumerates.
No successful entry to the download mode, GPIO operation or flash has been
observed. Physical connector/revision and adapter voltage must be verified
before any wiring.
