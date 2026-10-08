# Ideas, not built

Brainstormed 2026-09-21. Nothing here is started. The line input (PCM1808) note is in DESIGN.md.

## Named as wanted

- **Shake to retrigger.** Accelerometer jerk (magnitude minus 1 g), range set to 8 g so a flick
  does not saturate, threshold + 150 ms cooldown. Or the MPU6050 motion interrupt on spare
  GPIO 21. Resets the head to the loop start, or jumps to the next splice.
- **Roll axis as a wet control for one effect.** Left dry, right full. Effect chosen in the menu:
  resonant low pass (one small Faust file), or a crusher (bit depth + sample rate reduction,
  microGranny's Crush, a few lines of C).

## Worth doing next

- **Stability** (Blooper): one knob that ages the tape. Slow random speed wobble (wow/flutter),
  occasional dropouts, a little high loss. TAPE mode. One noise generator per block.
- **Splices** (Morphagene): a click drops a marker; the loop is the region between markers;
  tilt scrubs inside the splice; flick or shake moves to the next. Marker bookkeeping only.
- **Echo locked to the loop** (delay = loop length or a division) with feedback, pairing with
  **overdub with decay** (time lag accumulator). A dozen lines.
- **Reverb**: Faust `dm.zita_light`; the kit's Faust showcase already runs zita on this board.
- **Freeze on hold**: GRAIN mode, click sets speed 0 at the head, click again to run.

## Later

- Loop window from the joystick (a sub-case of splices).
- ~~Save and recall takes as WAV~~ done as 8 slots on flash (2026-09-21). USB drive mode still open.
- ~~Settings saved to NVS~~ done 2026-10-08 (`firmware/main/settings.c`).
- USB MIDI + USB audio + JUCE plugin: done 2026-09-21 (see README).
- Speed snapping to semitones as well as octaves and fifths.
- GRAIN mode on the C3 in Q15.
- ADC electret input option (red MAX4466-style module) on GPIO 0, ADC continuous DMA.

Sources: makenoisemusic.com/modules/morphagene, bastl-instruments.com/instruments/microgranny,
faustlibraries.grame.fr/libs/reverbs, faustdoc.grame.fr/tutorials/esp32.
