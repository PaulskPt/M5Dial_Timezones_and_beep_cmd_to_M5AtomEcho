/*
* Original version of the C++ ATOMSPK class file found at:
* https://github.com/m5stack/M5Atom/blob/master/examples/ATOM_BASE/ATOM_SPK/PlayRawPCM/AtomSPK.cpp
* In Arduino M5Stack ATOM_SPK library found in folder:
* C:\Users\<User>\Documents\Arduino\libraries\M5Atom\examples\ATOM_BASE\ATOM_SPK\PlayRawPCM
*
* 2024-10-06 Ported to M5Stack Atom Echo by @PaulskPt (Github)
* while renaming class name to ATOMECHOSPKR and renaming C++ files to AtomEchoSPKR.h and AtomEchoSPKR.cpp-
*/
#include "AtomEchoSPKR.h"

static QueueHandle_t i2sstateQueue    = nullptr;
static QueueHandle_t i2sAudioEndQueue = nullptr;

IRAM_ATTR double fastSin(double deg) {
    int integer = (int)deg % 360;
    // int point = deg - integer;
    return sinmap[integer];
}

void ECHOspeakerPlayTask(void *arg) {
  i2sQueueMsg_t QueueMsg;
  while (1)
  {
    if (xQueueReceive(i2sstateQueue, &QueueMsg, portMAX_DELAY) == pdTRUE) {
      if (QueueMsg.type == kTypeAudio) 
      {
        audioParameters_t *pam = (audioParameters_t *)QueueMsg.dataptr;
        size_t bytes_written   = 0;
        i2s_write(SPEAKER_I2S_NUMBER, pam->pAudioData, pam->length,
          &bytes_written, portMAX_DELAY);
        // Serial.printf("point :%p\r\n",pam->pAudioData);
        if (pam->freeFlag == true)
          xQueueSend(i2sAudioEndQueue, &pam->pAudioData,
          (TickType_t)0);
        // delay(1);
        // delete (uint16_t*)pam->pAudioData;
        delete pam;
      } 
      else if (QueueMsg.type == kTypeBeep)
      {
        beepParameters_t *pam = (beepParameters_t *)QueueMsg.dataptr;
        size_t bytes_written  = 0;
        size_t count = 0, length = 0;
        double t = (1 / (double)pam->freq) * (double)pam->rate;

        if (pam->time > 1000)
        {
          length = pam->rate * (pam->time % 1000) / 1000;
          count  = pam->time / 1000;
        } 
        else 
        {
          length = pam->rate * pam->time / 1000;
          count  = 0;
        }
        int rawLength = (count == 0) ? length : pam->rate;
        rawLength -= (int)((int)(rawLength % (int)t));
        int16_t *raw = (int16_t *)calloc(rawLength, sizeof(int16_t));
        for (int i = 0; i < rawLength; i++)
        {
          double val = 0;
          if (i < 1000) 
          {
            val = pam->maxval * i / 1000;
          }
          else if (i > (rawLength - 1000))
          {
            val = pam->maxval -
              pam->maxval * (1000 - (rawLength - i)) / 1000;
          }
          else
          {
            val = pam->maxval;
          }
          if (pam->freq == 0)
          {
            raw[i] = 0;
          }
          else
          {
            raw[i] = (int16_t)(((fastSin(360 * i / t) + 0) * val));
          }
        }
        if (rawLength != 0) 
        {
          i2s_write(SPEAKER_I2S_NUMBER, raw, (rawLength)*2,
            &bytes_written, portMAX_DELAY);
          Serial.printf("ATOMECHOSPKR::ECHOspeakerPlayTask(): I2S Write\r\n");
        }
        delete pam;
        delete raw;
      }
    }
    delay(1);
  }
}

bool ATOMECHOSPKR::begin(int __rate)
{
  char TAG1[] = "ATOMECHOSPKR::";
  char TAG2[] = "begin(): ";
  Serial.printf("%s%srate = %d\n", TAG1, TAG2, __rate);

  esp_err_t err = ESP_OK;

  i2s_driver_uninstall(SPEAKER_I2S_NUMBER);
  i2s_config_t i2s_config = 
  {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = __rate,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,  // is fixed at 12bit, stereo, MSB
    .channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 6, // was: 2,
    .dma_buf_len          = 60, // was: 128,
    .use_apll             = false,
    .tx_desc_auto_clear   = true
  };

  Serial.printf("%s%si2s_driver_installing\n", TAG1, TAG2);

  err += i2s_driver_install(SPEAKER_I2S_NUMBER, &i2s_config, 0, NULL);
  
  i2s_pin_config_t tx_pin_config =
  {
    .bck_io_num   = CONFIG_I2S_BCK_PIN,
    .ws_io_num    = CONFIG_I2S_LRCK_PIN,
    .data_out_num = CONFIG_I2S_DATA_PIN,
    .data_in_num  = CONFIG_I2S_DATA_IN_PIN
  };

#if (ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 3, 0))  // Copied from sketch M5Echo_PlayMusic.ino
  tx_pin_config.mck_io_num = I2S_PIN_NO_CHANGE;
#endif
  Serial.printf("%s%si2s_set_pin\n", TAG1, TAG2);
  err += i2s_set_pin(SPEAKER_I2S_NUMBER, &tx_pin_config);
  Serial.printf("%s%si2s_set_clk", TAG1, TAG2);
  err += i2s_set_clk(SPEAKER_I2S_NUMBER, __rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);

  _rate            = __rate;
  i2sAudioEndQueue = xQueueCreate(1024, sizeof(uint16_t *));
  i2sstateQueue    = xQueueCreate(1024, sizeof(i2sQueueMsg_t));
  if (i2sstateQueue == 0)
  {
    return false;
  }
  // Set initial volume
  int spkr_volume = 8;
  Serial.printf("%ssetVolume(): setting volume to: %d\n", TAG1, spkr_volume);
  ATOMECHOSPKR::setVolume(spkr_volume); // Volume level from 0 to 10

  xTaskCreatePinnedToCore(ECHOspeakerPlayTask, "ECHOspeakerPlayTask", 4096, nullptr,
    10, nullptr, 0);
  return (err == ESP_OK) ? true : false;
}

size_t ATOMECHOSPKR::playRAW(const uint8_t *__audioPtr, size_t __size, bool modal,
  bool freeFlag, TickType_t __ticksToWait)
{
  size_t writeSize = 0;
  if (modal == false)
  {
    audioParameters_t *pam =
      (audioParameters_t *)malloc(sizeof(audioParameters_t));
    pam->pAudioData   = (void *)__audioPtr;
    pam->length       = __size;
    pam->freeFlag     = freeFlag;
    i2sQueueMsg_t msg = {.type = kTypeAudio, .dataptr = pam};
    xQueueSend(i2sstateQueue, &msg, (TickType_t)0);
    // xTaskCreatePinnedToCore(speakerAudioTask, "speakerAudioTask", 4096,
    // pam, 3, NULL, 0);
  }
  else
  {
    i2s_write(SPEAKER_I2S_NUMBER, __audioPtr, __size, &writeSize, __ticksToWait);
  }
  return writeSize;
}

// Overloaded function created by @PaulskPt
size_t ATOMECHOSPKR::playBeep(beep __beep)
{
  size_t writeSize = 0;

  int __freq   = __beep.freq;
  int __timems = __beep.time_ms;
  int __maxval = __beep.maxval;
  bool __modal = __beep.modal;
  writeSize = ATOMECHOSPKR::playBeep(__freq, __timems, __maxval, __modal);
  return writeSize;
}

size_t ATOMECHOSPKR::playBeep(int __freq, int __timems, int __maxval, bool __modal)
{
  size_t writeSize = 0;

  if (__modal == false) 
  {
    beepParameters_t *pam =
        (beepParameters_t *)malloc(sizeof(beepParameters_t));
    pam->freq   = __freq;
    pam->time   = __timems;
    pam->rate   = _rate;
    pam->maxval = __maxval;

    i2sQueueMsg_t msg = {.type = kTypeBeep, .dataptr = pam};
    xQueueSend(i2sstateQueue, &msg, (TickType_t)0);
  }
  else
  {
    size_t bytes_written = 0;
    size_t count = 0, length = 0;

    double t = (1 / (double)__freq) * (double)_rate;

    if (__timems > 1000)
    {
      length = _rate * (__timems % 1000) / 1000;
      count  = __timems / 1000;
    }
    else
    {
      length = _rate * __timems / 1000;
      count  = 0;
    }
    int rawLength = (count == 0) ? length : _rate;

    uint16_t *raw = (uint16_t *)ps_calloc(rawLength, sizeof(uint16_t));

    for (int i = 0; i < rawLength; i++)
    {
      int val = 0;
      if (i < 100)
      {
        val = __maxval * i / 100;
      } 
      else if (i > (rawLength - 1000))
      {
        val = __maxval - __maxval * (1000 - (rawLength - i)) / 1000;
      }
      else
      {
        val = __maxval;
      }
      raw[i] = (uint16_t)((fastSin(360 / t * i)) * val);
    }

    for (int i = 0; i < count; i++)
    {
      i2s_write(SPEAKER_I2S_NUMBER, raw, _rate, &bytes_written, portMAX_DELAY);
    }

    if (length != 0)
    {
      i2s_write(SPEAKER_I2S_NUMBER, raw, length, &bytes_written, portMAX_DELAY);
    }
    delete raw;
  }
  return writeSize;
}

// This function added by @PaulskPt after consulting MS CoPilot, 
// however, I had to add &bytes_written parameter to the call to i2s_write
void ATOMECHOSPKR::setVolume(int volume)
{
  size_t bytes_written = 0;
  if (volume < 0)
    volume = 0;
  if (volume > 10)
    volume = 10;
    // Adjust the volume by setting the I2S DAC output level
    // This is a simplified example; actual implementation may vary
    int16_t sample = volume * 3276; // Scale volume to 16-bit sample
    i2s_write(SPEAKER_I2S_NUMBER, (const char*)&sample, sizeof(sample), &bytes_written, portMAX_DELAY);
}

