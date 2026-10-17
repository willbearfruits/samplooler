// Roll-tilt effects after the recorder.
//   roll left   tape-style delay: the angle sets how much goes in and how long the tail rings.
//               The wet return is always open, so rolling back flat lets the tail decay by itself.
//   roll right  stutter: the first tilt grabs a slice of what is playing and loops it; tilting
//               further shortens the loop, from a rhythmic repeat down to a buzzing pitch.
// No allocation in the audio path, no libm calls in fx_process.
#pragma once
void fx_init(void);
void fx_set_delay(float amount);     // 0..1 from the UI task, 0 = nothing new enters the delay
void fx_set_stutter(float amount);   // 0..1, 0 = off (releases the slice)
void fx_process(float *buf, int n);  // audio task only, in place
int  fx_delay_active(void);          // for the screen: tail still audible
int  fx_stutter_active(void);
