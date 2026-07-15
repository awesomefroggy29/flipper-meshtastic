# flipper-meshtastic

Turn your **Flipper Zero** into a portable **Meshtastic** node using an **Adafruit RFM95W 915 MHz LoRa module**.

This app allows the Flipper Zero to send and receive Meshtastic messages without requiring another microcontroller.

> [!WARNING]
> This project is intended for users in the **United States** and is designed for the **915 MHz LoRa band**.
>
> Radio laws vary by country. If you are outside the United States, check whether 915 MHz LoRa and Meshtastic operation are legal in your area before building or using this project.
>
> I am not responsible for any broken flipper zeros or radio modules and injuries from soldering. Proceed at your own risk.

## Before You Begin

Please read this entire guide from beginning to end before purchasing parts, soldering, or connecting anything.

This project requires basic soldering and electronics knowledge. Double-check every connection before powering the module.

## Parts Required

- Flipper Zero
- microSD card installed in the Flipper Zero
- USB cable that connects the Flipper Zero to your computer
- Soldering iron
  - A wedge tip is highly recommended for small electronics
- Soldering tin
- 6 male pin headers
- 6 male-to-female jumper wires
- 1 male-to-male jumper wire
- A proper 915 MHz LoRa antenna, or a paperclip for a basic temporary antenna
- [Adafruit RFM95W 915 MHz LoRa Radio Transceiver Breakout — Product 3072](https://www.adafruit.com/product/3072)

> [!IMPORTANT]
> Buy **Adafruit Product 3072**, which is the 915 MHz version.
>
> Do **not** buy Product 3073. Product 3073 is the 433 MHz version and is not compatible with this project's current configuration.

## Install the App

1. Download the latest `.fap` file from the [Releases page](https://github.com/awesomefroggy29/flipper-meshtastic/releases).
2. Connect your Flipper Zero to your computer with USB.
3. Open [Flipper Lab](https://lab.flipper.net/) and press **Connect**.
4. Select your Flipper Zero in the browser popup.
5. In the sidebar, open:

   ```text
   Files > ext > apps
   ```

6. Press the **+** button and create a folder named:

   ```text
   meshtastic
   ```

7. Open the folder, press the **+** button again, and select **Upload file**.
8. Upload:

   ```text
   fliptastic.fap
   ```

## Hardware Assembly

> [!CAUTION]
> Soldering irons become extremely hot. Learn how to safely solder small electronics before attempting this build.
> Watching a tutorial video online is HIGHLY recommended

### 1. Solder the Pin Headers

Solder the short end of a male pin header to each of these labeled pads on the RFM95W breakout board:

- `GND`
- `MOSI`
- `MISO`
- `CS`
- `SCK`
- `G0`

The long ends of the headers should point downward when viewed from the top of the board.

For `VIN`, solder one end of a male-to-male jumper wire directly to the `VIN` pad so the other end exits from the top side of the board.



### 2. Add an Antenna

For the antenna, a straight piece of wire or paperclip approximately **8.2 cm long** can be used as a quarter-wave antenna, and will be better than any coiled antenna if you make it right.

1. Straighten the paperclip as much as possible.
2. Cut a piece as close to **8.2 cm** as possible.
3. Slightly longer is preferable to shorter.
4. Optionally bend approximately 3–4 mm at one end so the antenna can point upward.
5. Solder that end to the antenna pad above the **A** at the end of the `LoRa` label on the board.

> [!WARNING]
> Never transmit without an antenna connected. Doing so will damage the radio module.

### 3. Connect the RFM95W to the Flipper Zero

Connect the female end of each female to male pins onto the corresponding male pin on the radio module, and each male end of the jumper wire to the corresponding pin on the flipper.

| RFM95W pin | Flipper Zero GPIO pin |
|---|---|
| `MOSI` | Pin 2 / `A7` |
| `MISO` | Pin 3 / `A6` |
| `CS` | Pin 4 / `A4` |
| `SCK` | Pin 5 / `B3` |
| `G0` | Pin 6 / `B2` |
| `GND` | Pin 8 / `GND` |
| `VIN` | Pin 9 / `3V3` |

> [!CAUTION]
> Do not mix up `MOSI` and `MISO`.
>
> Triple-check every wire before powering the module. Incorrect wiring could damage the Flipper Zero or RFM95W.

## Example Wire Routing

These photos show one way to wrap the wires so the module can sit on top of the Flipper Zero.

<img width="384" height="512" alt="RFM95W wire routing example 1" src="https://github.com/user-attachments/assets/d1429632-bd83-4453-a0ad-491a28bc4418">

<img width="512" height="384" alt="RFM95W wire routing example 2" src="https://github.com/user-attachments/assets/3e10238a-8f1d-4ad1-b2f0-fc81cba550e3">

## First Startup

After checking all wiring:

1. Open **Apps** on your Flipper Zero.
2. Open the folder where you installed the app.
3. Launch **Fliptastic**.

If the app opens to a menu with inbox at the top, the radio was detected successfully. If it opens to "RFM95W not found," then see below.

The Flipper will automatically broadcast its node name and ID on each app start.

## Features

- Send and receive Meshtastic messages
- Automatic acknowledgements
- Channel and frequency settings
- Custom node name
- Politeness for the airwaves
- Client/Client Mute modes
- Weather recieving
- Position broadcasting (will broadcast its position if there is a node directly communicatable without 1 or more hops

## Message Status Symbols

| Symbol | Meaning |
|---|---|
| `-` | Waiting for an acknowledgement |
| `+` | The message was acknowledged |
| `=` | The message was not acknowledged |
| `/` | The channel remained busy and the message was not sent |
| `<>` | The message probably contained an unsupported emoji or special character |

### Channel Busy

Before transmitting, the app checks whether the radio channel is already active.

If the channel is busy, the app waits instead of transmitting over another packet. If it remains busy until the timeout expires, the app displays **Channel is busy** and marks the message with `/`.

This politeness behavior applies when sending the Flipper's own packets and, where applicable, when rebroadcasting received packets.

## Troubleshooting

### RFM95W Not Found

Possible causes include:

- Incorrect wiring
- A weak or incomplete solder joint
- Loose jumper wires
- Reversed `MOSI` and `MISO`
- Incorrect power connection
- The wrong RFM95W frequency version
- A damaged module

Disconnect power before changing any wires.

### Messages Are Not Acknowledged

Possible reasons include:

- No compatible node was close enough to receive the message
- The receiving node did not send an acknowledgement
- The channel or frequency settings do not match nearby nodes
- Signal strength was too weak for another node to see it
- The channel was congested

### Poor Range

Check that:

- An antenna is connected
- The antenna is designed for 915 MHz
- The antenna connection is secure
- The module is not surrounded by metal
- Nearby nodes use compatible Meshtastic settings

Coiled antennas are not very recommended as they have poor range, a paperclip antenna may work 

## Support the Project

If this project helped you, consider starring the repository.

Thanks for checking out **flipper-meshtastic**!
