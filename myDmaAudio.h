/*****
  simple mono sound player for multiple simultaneous voices
    files need to have same sample rate
  
   Uses i2s-bus/dma to push data to DAC1 and 2
   Mix of sounds done in seperate task.
   If mix of levels get too high value, value is set to max possible
   AUDIOSIZE sets buffer size and maximum delay

Usage example:
   #include "myDmaAudio.h"
   
   Player play[3];  
   WavData mySounds[2]= { wav1data, wav2data };  // 8-bit mono

   int rate= mySounds[0].getSampleRate()

   initAudio(rate);   // No value=44100 (rate used for all sounds)

   play[0].play(mySounds[0]);
   play[1].play(mySounds[1]);
   play[2].play(mySounds[0]);
   while(1) {
     if ( ! play[0].active()) {  // restart when done
       play[0].play(mySounds[0]); 
     }
     delay(1000);
     play[1].play(mySounds[1]);  // start every sec (abort earlier if longer)
   }

   // play[0].actice()  to check if player[0] is playing if you want to wait for new sound
   // play[0].play(mySounds[0]); replaces current sound if already playing
   // doAudio= 0; turns audio task off
   
 ***/
 
#include "driver/i2s.h"
#include "freertos/queue.h"
#include <list>

// wav format
#define WAV_SAMPLERATE_L 24  // sample rate data locations
#define WAV_SAMPLERATE_H 25
#define WAV_FILESIZE_L 40 // LSB PCM data size location 
#define WAV_FILESIZE_M 41
#define WAV_FILESIZE_H 42 // MSB
#define WAV_DATA_START 44 // this is where the data starts


// Wav data class
class WavData {
 public:
 WavData(unsigned char *_data) : data(_data) {
    
    sampleRate=
      data[WAV_SAMPLERATE_H]*256 +
      data[WAV_SAMPLERATE_L]; 
    
    dataSize=
      data[WAV_FILESIZE_H]*65536 +
      data[WAV_FILESIZE_M]*256 +
      data[WAV_FILESIZE_L];
    
    data += WAV_DATA_START;	
  }
  
  float getDuration() {
    return dataSize / (float)sampleRate; // in sec.
  }
  
  uint16_t getSampleRate() {
    return sampleRate;
  }

  // variables
  uint16_t sampleRate;
  uint32_t dataSize;
  unsigned char *data;    
};


// driver variables
volatile int doAudio= 1;
TaskHandle_t myAudioTask;
portMUX_TYPE myAudioMutex = portMUX_INITIALIZER_UNLOCKED;
#define  i2s_num  I2S_NUM_0
#ifndef AUDIOSIZE
#define AUDIOSIZE 40
#endif

class Player;

std::list<Player*> thePlayers;

class Player {
 public:
  Player() {
    pos= endPos= 0;
    portENTER_CRITICAL(&myAudioMutex);
    thePlayers.push_back(this);
    portEXIT_CRITICAL(&myAudioMutex);  
  }
  ~Player() {
    portENTER_CRITICAL(&myAudioMutex);
    thePlayers.remove(this);
    portEXIT_CRITICAL(&myAudioMutex);      
  }
  
  void play(WavData &w) {    
    portENTER_CRITICAL(&myAudioMutex);
    pos= w.data;
    endPos= pos + w.dataSize;
    portEXIT_CRITICAL(&myAudioMutex);
  }
  bool active() {
    portENTER_CRITICAL(&myAudioMutex);
    bool status= pos<endPos;
    portEXIT_CRITICAL(&myAudioMutex);
    return status;
  }
  
  volatile unsigned char *pos;
  volatile unsigned char *endPos;  
};



// mix data from players to i2s->dac
void audioTaskLoop(void *param)
{
  uint16_t src[AUDIOSIZE*2];
  
  while (doAudio) {
    size_t written;
    size_t size=0;    

    // fill send buffer
    for (int i=0; i<AUDIOSIZE; i++) {
      int sum= 127*256; // default level
      uint16_t count=0;
      
      // mix input (don't change input while mixing)
      portENTER_CRITICAL(&myAudioMutex);
      for (auto p : thePlayers) {	
	if (p->pos < p->endPos) {
	  unsigned int v= *(p->pos);
	  sum += v*256;	  
	  (p->pos)++;
	  count++;
	}
      }
      portEXIT_CRITICAL(&myAudioMutex);
      if (count!=0) {
	sum -= 127*256*count; // remove 0 offset from sources
      }
      
      if (sum>0xffff) sum= 0xffff;
      if (sum<0) sum= 0;
      src[i*2+0]= sum; 
      src[i*2+1]= sum; 
      size++;
    }

    // fill buffer and wait for more space
    esp_err_t status= i2s_write(i2s_num, src, size, &written, portMAX_DELAY);       if (written != AUDIOSIZE) {
      //strange, there isn't a timeout
    }
  }
  
  i2s_driver_uninstall(i2s_num); //stop & destroy i2s driver
}

i2s_config_t i2scfg;

void initAudio(int sampleRate=44100)
{
  esp_err_t ret;
 
  i2scfg.mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX |
			      I2S_MODE_DAC_BUILT_IN);
  i2scfg.sample_rate = sampleRate/4;
  i2scfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2scfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S_MSB);
  i2scfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2scfg.intr_alloc_flags = 0; // default interrupt priority
  i2scfg.dma_buf_count = 2;
  i2scfg.dma_buf_len = AUDIOSIZE;
  i2scfg.use_apll = false;
  i2scfg.fixed_mclk= 0;
  
  ret= i2s_driver_install(i2s_num, &i2scfg, 0, NULL); // (No OS queue)
  assert(ret == ESP_OK);

  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
  /*
    i2s_set_pin(i2s_num, NULL); // internal DAC, both channels
  
    i2s_set_clk(i2s_num, sampleRate/2,
       I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);



  
  //You can call i2s_set_dac_mode to set built-in DAC output mode.
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);   

  i2s_set_sample_rates(i2s_num, sampleRate); //set sample rates
  
  */
  doAudio= 1;

  xTaskCreatePinnedToCore(
      audioTaskLoop, /* Function to implement the task */
      "audioTask", /* Name of the task */
      1000,  /* Stack size in words */
      NULL,  /* Task input parameter */
      0,  /* Priority of the task */
      &myAudioTask,  /* Task handle. */
      0); /* Core where the task should run */

}
