// The effects after the recorder: the roll's delay and stutter, then the filter (the hand, or the left knob).
//   roll left   tape-style delay: the angle sets how much goes in and how long the tail rings.
//               The wet return is always open, so rolling back flat lets the tail decay by itself.
//   roll right  stutter: the first tilt grabs a slice of what is playing and loops it; tilting
//               further shortens the loop, from a rhythmic repeat down to a buzzing pitch.
//   filter      one sweep: below 0 a low-pass closes (12 kHz down to 150 Hz at -1), above 0 a high-pass rises
//               (30 Hz up to 5 kHz at +1), 0 is open and costs nothing. It comes last, so it darkens the delay's
//               tail too, like a hand over the speaker.
// No allocation in the audio path, no libm calls in fx_process.
#pragma once
void fx_init(void);
void fx_set_delay(float amount);     // 0..1 from the UI task, 0 = nothing new enters the delay
void fx_set_stutter(float amount);   // 0..1, 0 = off (releases the slice)
void fx_set_filter(float pos, float res);   // UI task: -1..1 and the resonance 0..0.9 (calls libm; the audio task only reads the result)
float fx_filter_pos(void);           // where the filter is asked to be, for the screen
void fx_process(float *buf, int n);  // audio task only, in place
int  fx_delay_active(void);          // for the screen: tail still audible
int  fx_stutter_active(void);
