# update-watch

State for the scheduled "CrossInk Update-Check" task. Not firmware code.

- `crosspoint.last_notified_release`: newest stable CrossPoint Reader release the owner was told about.
- `crosspoint.awaiting_crossink`: true while that CrossPoint release is not yet in a CrossInk release.
- `crossink.built_release`: CrossInk release that the `crossink-custom` branch is currently based on.
