<!--
type: entry point (overview, installation, setup)
audience: functional
reader: Owner of an Apollo MTR-1 and a WLED strip, with or without Home Assistant. Can flash ESPHome and change WLED settings. Has this repository, their own sensor and their own WLED controller. Does not read the firmware source.
owner: follow-me-stairs maintainer
last_verified: 2026-10-14. Behaviour, settings and wizard screens checked against the firmware and wizard source. Installation steps checked against example/apollo-mtr-1.yaml, not run on a new device.
sources: firmware/, wizard/wizard.html, example/apollo-mtr-1.yaml
-->

# follow-me-stairs

A stair light that follows you. A radar sensor at the stairs tracks where each person is. An LED strip along the stairs lights the part next to that person, and the lit part moves with them, up or down.

The parts:

| Part | Job |
|---|---|
| Radar sensor (Apollo MTR-1) | Runs this firmware. It tracks people, decides which LEDs to light, and sends the LED colors to WLED. |
| WLED controller | Drives the LED strip. It runs standard WLED, with no changes. |
| Your phone or computer | Runs the setup wizard once, in a web browser. The sensor serves the wizard itself. |
| Home Assistant | Optional. The sensor works without it. |

The sensor sends the LED colors to WLED directly over your network. Home Assistant is not needed for the light to work.

## What it does

- Lights a window of LEDs next to each person on the stairs, and moves it with them.
- Follows up to three people at the same time, with one window each.
- Lights the end of the stairs when a person comes near it, before they step on.
- Shows a moving effect inside the window, for example Rainbow, Christmas or Fireplace.
- Lights a dim night light at each end of the stairs, when you turn it on.
- Steps aside while you use WLED yourself, and starts again when you turn WLED off.

All values for your stairs come from the setup wizard: the shape, the number of steps, where the strip runs and the WLED address. The wizard stores them on the sensor. The firmware has no values for any one house.

## What you need

- An Apollo MTR-1. Another ESP32 with an LD2450 radar can work, on ESPHome's esp-idf framework. See [Using a different device](#using-a-different-device).
- ESPHome 2026.8.2 or newer. Firmware built with ESPHome 2026.3.3 crashes shortly after it starts.
- An LED strip along the stairs, on a WLED controller. Follow-me is tested with WLED 16.
- The sensor and the WLED controller on the same network.
- Straight, L-shaped or U-shaped stairs, with at most one landing.

## Installing from the browser

Open the [follow-me installer](https://randompedestrian07.github.io/follow-me-stairs/) in Chrome or Edge on a computer, and connect the MTR-1 with a USB-C data cable. The installer flashes the firmware and then asks for your Wi-Fi. Installing erases the sensor. A sensor that already has a follow-me setup must go through the setup wizard again.

The installer firmware has no Wi-Fi credentials. If the Wi-Fi step does not appear, connect a phone to the `Apollo MTR1 Hotspot` network and enter your Wi-Fi there.

To change the firmware yourself, install with ESPHome instead.

## Installing on an Apollo MTR-1 with ESPHome

1. Copy `example/apollo-mtr-1.yaml` into your ESPHome configuration folder.
2. Put this repository in the same folder, as a folder named `follow-me-stairs`.
3. Add `wifi_ssid` and `wifi_password` to `secrets.yaml` in that folder.
4. Install the configuration on the sensor with ESPHome.
5. Open `http://<sensor address>/` in a browser.

The sensor's web page shows the Follow-Me controls. Follow-Me Setup shows "Not set up" until you complete the setup wizard.

### Using a different device

Add the follow-me package to the device's own configuration:

```yaml
substitutions:
  follow_me_dir: <path to this repository>/firmware   # relative to the device file

packages:
  follow_me: !include <path to this repository>/firmware/follow-me.yaml
```

ESPHome finds the firmware's source files through `follow_me_dir`, from the folder of the device file.

Follow-me finds the radar's position readings by name. If your radar's target sensors are not named `Target-1 X`, `Target-1 Y` and so on, set these substitutions. In each name, `%d` stands for the target number, 1 to 3.

| Substitution | Default |
|---|---|
| `radar_x_name` | `Target-%d X` |
| `radar_y_name` | `Target-%d Y` |

Follow-me adds a web server to the device. If the device configuration already has `web_server:`, ESPHome combines the two.

## Setting up

Open the setup wizard from the "Set up Follow-Me" button on the sensor's web page, or at `http://<sensor address>/followme`. Each screen keeps Next off until you answer it.

1. **Shape:** pick straight, L-shaped or U-shaped. Check which way the stairs turn, and flip it if the drawing is wrong.
2. **Steps:** count the steps in each flight, and give the stair width. Do not count a landing or the upper floor.
3. **Strip:** tap the wall that the strip is on. Then pick the end where LED 1 is.
4. **Walk:** start on the lower floor and walk up once. Stop on each step that the screen marks, and tap. At the end, the screen grades the walk. "Good walk" means you can continue. "Walk it again" names the part of the stairs to walk again.
5. **Match:** enter the WLED address. Tap Read to get the LED count and to check WLED's settings. For each mark on the drawing, move the lit LED until it is at that spot on your stairs. The bottom and top marks are required. Then tap "Save to sensor".
6. **Test:** walk the stairs and change the sliders until the light is correct for you. The sliders change the sensor's settings directly.

When the setup is saved, Follow-Me Setup shows Ready and the light follows you.

To change the setup later, open the wizard again. It loads the setup saved on the sensor.

## Settings

All settings are on the sensor's web page. When the sensor is added to Home Assistant, they are also on its device page. Distances are in steps: one step is the average tread depth that the setup wizard measured.

For ranges, defaults and the full description of each setting, see [docs/settings.md](docs/settings.md).

| Control | What it does |
|---|---|
| Follow-Me Glow | The light that follows people. |
| Follow-Me Night Light | Lights a patch at each end of the stairs. |
| Follow-Me Window | The length of the lit window. |
| Follow-Me Buffer | How close the person gets to either end of the window before the window moves. |
| Follow-Me Offset | Moves the window ahead of the person, in the direction they walk. |
| Follow-Me Approach | A person off the stairs within this distance of either end lights that end of the stairs. |
| Follow-Me Smoothness | How closely the window follows the radar. |
| Follow-Me Hold | How long the window stays lit after the radar loses a person on the stairs. |
| Follow-Me Fade In | The fade-in time of a window that starts away from the ends of the stairs. |
| Follow-Me Fade Out | The fade-out time after Follow-Me Hold ends. |
| Follow-Me Night Light Size | The length of each night light patch. |
| Follow-Me Multi-Person | Off: one window follows one person. On: up to three windows follow up to three people. |
| Follow-Me Pause While WLED On | Lets WLED take over the strip while WLED itself is on. |
| Follow-Me RGB White | For strips with no white LED only. Sets how the sensor mixes white from red, green and blue: Warm, Neutral or Cool. |
| Follow-Me Setup | What follow-me needs before it can work. |
| Follow-Me Paused | On while WLED itself is on. |
| Follow-Me Position | Where the followed person is, in steps down from the top edge of the top step. |

## WLED

WLED needs some settings changed before it accepts the light from the sensor. The setup wizard checks them on the Match screen, and "Fix WLED settings" changes them. For the list, see [wled/README.md](wled/README.md).

## Known limits

- Only strips with a white LED (RGBW) are tested on hardware.
- Stairs with more than one landing, winders and spiral stairs are not currently supported.
- A WLED address given as a `.local` name works only if the sensor can resolve it. An IP address with a DHCP reservation always works.
- The sensor's web page and the setup wizard have no password. Anyone on your network can open them.

## More documents

| Document | Contents |
|---|---|
| [docs/settings.md](docs/settings.md) | Every setting: what it does, its range and its default. What each Follow-Me Setup status means. |
| [wled/README.md](wled/README.md) | The WLED settings that follow-me needs. |
| [docs/developing.md](docs/developing.md) | How the repository is organized, and how to change and test the wizard and the firmware. |

## License

MIT, see `LICENSE`. IBM Plex fonts: SIL Open Font License 1.1, see `wizard/fonts/OFL.txt`.
