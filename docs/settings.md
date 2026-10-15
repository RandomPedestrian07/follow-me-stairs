<!--
type: reference
audience: functional
reader: Owner of an Apollo MTR-1 and a WLED strip, with or without Home Assistant. Can flash ESPHome and change WLED settings. Has this repository, their own sensor and their own WLED controller. Does not read the firmware source.
owner: follow-me-stairs maintainer
last_verified: 2026-10-14. Names, ranges, defaults and behaviour checked against firmware/follow-me.yaml and firmware/follow-me.h. Not checked: how each control renders on the sensor's web page. Defaults confirmed on hardware with the published 2026.10.3 firmware.
sources: firmware/follow-me.yaml, firmware/follow-me.h, firmware/follow-me-calibration.h
-->

# Follow-Me settings

This document lists every control that follow-me adds to the sensor. For installation and setup, see the [README](../README.md).

## Where the settings are

| Place | Contents |
|---|---|
| The sensor's web page, `http://<sensor address>/` | Every control in this document |
| Home Assistant, on the sensor's device page | Every control in this document, when the sensor is added to Home Assistant |
| The Test screen of the setup wizard | Window, Buffer, Smoothness, Hold, Offset and Approach |

A change applies immediately. The sensor keeps every setting after a restart.

## Steps

Distances are in steps. One step is the average tread depth that the setup wizard measured on your stairs. A Window of 3 steps lights about three treads on any staircase.

## Lights

| Control | What it does | Default |
|---|---|---|
| Follow-Me Glow | The light that follows people. Off turns follow-me off. It sets the brightness, color, white level and effect of the window. | On, white only, 40% |
| Follow-Me Night Light | Lights a patch at each end of the stairs. It works when Follow-Me Glow is on or off. | Off. When turned on: white only, 5% |

Follow-Me Glow effects: Rainbow, New Year's, Valentine's, St. Patrick's, Easter, Fourth of July, Autumn, Halloween, Haunted, Hanukkah, Christmas, Candy Cane, Snowfall, Fireplace, Aurora, Ocean. An effect replaces the color with a moving pattern, inside the window. The pattern stays fixed to the stairs, and the person walks through it. When Follow-Me Glow turns off, the effect goes back to None.

Where the window and the night light overlap, each LED shows the brighter of the two.

## Tuning

| Control | What it does | Range | Default |
|---|---|---|---|
| Follow-Me Window | The length of the lit window. | 1 to 15 steps | 3 steps |
| Follow-Me Buffer | How close the person gets to either end of the window before the window moves. The window stays still while the person is inside it. When the person comes within this distance of either end of the window, the window slides to keep this distance. | 0 to 5 steps | 1 step |
| Follow-Me Offset | Moves the window ahead of the person, in the direction they walk. A negative value moves it behind. The shift grows with walking speed, so a person who stands still gets no shift. | -3 to 3 steps | 0 steps |
| Follow-Me Approach | A person off the stairs within this distance of either end lights that end of the stairs. The sensor ignores people farther away. | 0 to 6 steps | 2 steps |
| Follow-Me Smoothness | How closely the window follows the radar. 0% follows each radar reading. 100% moves slowly and smoothly. | 0 to 100% | 50% |
| Follow-Me Hold | How long the window stays lit after the radar loses a person on the stairs. Then the window fades out. | 0 to 60 s | 4 s |
| Follow-Me Fade In | The fade-in time of a window that starts away from the ends of the stairs. A window that starts at either end slides in from that end in 0.25 s. | 0.1 to 10 s | 1.5 s |
| Follow-Me Fade Out | The fade-out time after Follow-Me Hold ends. When a person walks off either end of the stairs, the window shrinks toward that end and fades out in 1 s. | 0.1 to 10 s | 3 s |
| Follow-Me Night Light Size | The length of each night light patch. | 0.5 to 8 steps | 2 steps |

## Switches and choices

| Control | What it does | Default |
|---|---|---|
| Follow-Me Multi-Person | Off: one window follows one person. On: up to three windows follow up to three people. With it on, a new window starts only near either end of the stairs. | Off |
| Follow-Me Pause While WLED On | Lets WLED take over the strip while WLED itself is on. On: while WLED itself is on, follow-me sends nothing and the strip shows WLED's own state. WLED can be turned on from its app, its button or Home Assistant. Follow-me starts again when WLED turns off. Off: follow-me lights the strip when WLED is on or off. | On |
| Follow-Me RGB White | For strips with no white LED only. Sets how the sensor mixes white from red, green and blue: Warm, Neutral or Cool. It applies to Follow-Me Glow and Follow-Me Night Light. | Neutral |

## Status

These controls are read-only.

| Control | What it shows |
|---|---|
| Follow-Me Setup | What follow-me needs before it can work. See the table below. |
| Follow-Me Paused | On while WLED itself is on. When the sensor cannot reach WLED, it shows off. |
| Follow-Me Position | Where the followed person is, in steps down from the top edge of the top step. Empty when nobody is followed. It updates every second. |

| Follow-Me Setup value | Meaning | What to do |
|---|---|---|
| Ready | Follow-me is working. | Nothing. |
| Not set up | The setup wizard has not saved a setup to this sensor. | Open the setup wizard and complete it. |
| Radar not found | The sensor cannot find the radar's position readings by name. | Set `radar_x_name` and `radar_y_name` in the device configuration to the names of the radar's target X and Y sensors. See the [README](../README.md#using-a-different-device). |
| WLED not found | The sensor cannot reach WLED at the address saved by the setup wizard. | Make sure WLED is on the network at that address. To change the address, open the setup wizard and go to the Match screen. |
| No web server | The device configuration has no web server, so the setup wizard cannot run. | Add `web_server:` to the device configuration and install it again. |

Until Follow-Me Setup shows Ready, the strip stays dark.
