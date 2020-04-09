# myDmaAudio

Audio with multiple sound sources for arduino on esp32<br> 

Plays to DAC1 and DAC2 using DMA + separate task for mixing sounds.

Usage example:
```
#include "myDmaAudio.h"

#include "wavdata1.h"
#include "wavdata2.h"
   
Player player[3];  // example with 3 simultaneous sound players
WavData mySounds[2]= { wav1data, wav2data };  // 8-bit mono wav files

void setup() {
   int rate= mySounds[0].getSampleRate(); // get sample rate from file

   initAudio(rate);

   player[0].play(mySounds[0]); // make some sounds
   player[1].play(mySounds[1]);
}

void loop() {

   if ( ! player[0].active()) {  // start sound when player not active
      player[0].play(mySounds[0]); 
   }
   
   if (...some event here...) {
      player[1].play(mySounds[0]);
   }
   
   if (...some event here...) {
      player[2].play(mySounds[1]);
   }
}

```
