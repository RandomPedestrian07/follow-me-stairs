# WLED

## Settings follow-me needs

Follow-me streams frames to the WLED controller over DDP (UDP port 4048)
while a person is on the stairs. WLED's own state stays off; the stream lights
the strip by itself, and the strip returns to WLED's own state after WLED's
realtime timeout (2.5 s by default) once frames stop.

The setup wizard's "Read" checks these settings and "Fix WLED settings" writes
them.

| Setting | Value | Why |
|---|---|---|
| Sync interfaces → Realtime → Receive UDP realtime | On | Turns on realtime input, DDP included |
| Realtime → Use main segment only | Off | When on, realtime LED numbers become segment-relative and the wizard's LED marks shift |
| Realtime → LED offset | 0 | Any offset shifts every LED |
| Realtime → Force max brightness | On | The stream sets brightness; WLED's master brightness must not scale it again |
| Realtime → Disable realtime gamma correction | On | The firmware applies its own gamma (2.2) |
| LED preferences → Turn LEDs on after power up | Off (with "Pause While WLED On" set) | While WLED is on, follow-me pauses (manual control wins); a WLED that boots on would leave follow-me paused after every power cycle |
| LED type / count | Set to match the strip | Follow-me reads both from WLED |

Manual control: the firmware polls WLED's `/json/state`. With "Follow-Me Pause
While WLED On" set (the default), while WLED reports itself on (turned on from Home Assistant, the WLED app or its button),
follow-me sends nothing and makes WLED leave realtime mode at once.

Tested with WLED 16 (RGBW strip).

## Glow rules

The firmware's glow follows rules found while tuning the look on a real strip:

- No temporal dithering. At the lowest brightness steps the toggling is
  visible as flicker.
- No fading trail.
- The glow shape reaches exactly zero at its edge. A Gaussian lingers at the
  lowest steps, where each 8-bit step is visible.
- Brightness comes only from the stream, not WLED's master brightness, to
  avoid quantising twice.
- Gamma 2.2 before quantising to 8 bits.
