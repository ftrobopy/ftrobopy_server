/*
The MIT License (MIT)

Copyright (c) 2022 by Torsten Stuehn

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <sys/param.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <iostream>
#include <sstream>
#include <thread>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <string>
#include <chrono>
#include <tuple>
#include <ft/ft.hpp>
#include "kissnet.hpp"
#include "cppystruct.h"

#define VERSION "0.9.4"

/*
__author__      = "Torsten Stuehn"
__copyright__   = "Copyright 2022 by Torsten Stuehn"
__credits__     = "fischertechnik GmbH"
__license__     = "MIT License"
__version__     = "0.9.4"
__maintainer__  = "Torsten Stuehn"
__email__       = "stuehn@mailbox.org"
__status__      = "beta"
__date__        = "05/08/2022"
*/

namespace kn = kissnet;
using std::cout;
using std::endl;
using namespace std::chrono_literals;
using std::this_thread::sleep_for;

// const std::string m_devicename = "ftrobopy_server";
const std::string m_devicename = "TX2013";
const std::string ftrobopy_server_version = VERSION;

const unsigned m_version = 0x4070000;   // ROBOPro-Version 4.7.0
//const unsigned m_version = 0x4060600; // ROBOPro-Version 4.6.6
//const unsigned m_version = 0x4040400; // ROBOPro-Version 4.4.4
//const unsigned m_version = 0x4020400; // ROBOPro-Version 4.2.4

#define MAXNUMTXTS 9  // maximum=9 (1 master + 8 slaves)

#define N_CAPTURE_BUFS 2

bool camera_is_online = false;
bool i2c_is_online = false;
bool main_is_running = false;
bool connected = false;

class Camera {
public:
  int videv;
  unsigned int bufidx;
  int format;
  int width, height, fps;
  struct buffer {
    void   *start;
    size_t  length;
  };
  struct buffer *buffers;

  Camera(int width, int height, int fps);
  Camera(const Camera& other) = delete;
  Camera& operator=(const Camera& other) = delete;
  Camera(Camera&& other) noexcept;
  Camera& operator=(Camera&& other) noexcept;
  ~Camera();
  
  int xioctl(int fd, int request, void *arg);
  int status(void);
  std::tuple<char*, int> getFrame(void);
};

Camera::Camera(Camera&& other) noexcept {
  videv = other.videv;
  bufidx = other.bufidx;
  format = other.format;
  width = other.width;
  height = other.height;
  fps = other.fps;
  other.videv = -1;
  buffers = other.buffers;
  other.buffers = NULL;
}

Camera& Camera::operator=(Camera&& other) noexcept {
  if (this != &other) {
    videv = other.videv;
    other.videv = -1;
    buffers = other.buffers;
    other.buffers = NULL;
  }
  return(*this);
}

Camera::~Camera(){
  enum v4l2_buf_type type;
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if(xioctl(videv, VIDIOC_STREAMOFF, &type) == -1) {
    cout << "error stop streaming in camClose" << endl;
    //return(0);
  }
  if (buffers!=NULL) {
    for (int i = 0; i < N_CAPTURE_BUFS; i++) {
      if (munmap(buffers[i].start, buffers[i].length) == -1) {
        cout << "error unmapping memory buffers in camClose" << endl;
        //return(0);
        break;
      }
    }
    free(buffers);
  }
  if (videv != -1) {
    close(videv);
  }
}

Camera::Camera(int width_, int height_, int fps_) :
  width(width_), height(height_), fps(fps_), videv(-1) 
{
  format = V4L2_PIX_FMT_MJPEG; // alternative: V4L2_PIX_FMT_YUYV or V4L2_PIX_FMT_MJPEG
  videv = open("/dev/video0", O_RDWR, 0);
  if (videv == -1) {
    cout << "error open video device" << endl;
    //return(0);
  }
  struct v4l2_format fmt = {0};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.pixelformat = format;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (xioctl(videv, VIDIOC_S_FMT, &fmt)==-1) { 
    cout << "error set pixel format in cam_init" << endl;
    //return(0);
  }

  struct v4l2_streamparm parm = {0};
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  parm.parm.capture.timeperframe.numerator=1;
  parm.parm.capture.timeperframe.denominator=fps;

  if (xioctl(videv, VIDIOC_S_PARM, &parm)==-1) { 
    cout << "error set framerate in cam_init" << endl;
    //return(0);
  }
 
  struct v4l2_requestbuffers req = {0};
  req.count = N_CAPTURE_BUFS;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (xioctl(videv, VIDIOC_REQBUFS, &req)==-1) {
    cout << "error request buffer in cam_init" << endl;
    //return(0);
  }

  if (req.count < N_CAPTURE_BUFS) {
    cout << "error insufficient capture buffer memory in cam_init" << endl;
    //return(0);
  }

  buffers = (buffer*)calloc(req.count, sizeof(*buffers));
  if (!buffers) {
    cout << "error out of memory in cam_init" << endl;
    //return(0);
  }

  for (bufidx = 0; bufidx<req.count; bufidx++) {
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = bufidx;
    if(xioctl(videv, VIDIOC_QUERYBUF, &buf)==-1) {
      cout << "error query buffer in cam_init" << endl;
      //return(0);
    }
    buffers[bufidx].length = buf.length;
    buffers[bufidx].start = mmap (NULL,
                                  buf.length,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED,
                                  videv, buf.m.offset);
    if (buffers[bufidx].start==MAP_FAILED) {
      cout << "error mmap failed in cam_init" << endl;
      //return(0);
    }

    struct v4l2_buffer buf2 = {0};
    buf2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf2.memory = V4L2_MEMORY_MMAP;
    buf2.index = bufidx;
    if(xioctl(videv, VIDIOC_QBUF, &buf2)==-1) {
      cout << "error query buffer2 in cam_init" << endl;
      //return(0);
    }
  }

  struct v4l2_control control = {0};
  control.id = V4L2_CID_POWER_LINE_FREQUENCY;
  control.value = 1; // 0=off 1=50Hz 2=60Hz;
  if(xioctl(videv, VIDIOC_S_CTRL, &control)==-1) {
    cout << "error set power line frequency in cam_init" << endl;
    //return(0);
  }

  struct v4l2_control control2 = {0};
  control2.id = V4L2_CID_SHARPNESS;
  control2.value = 0; // switch off sharpness correction of camera
  if(xioctl(videv, VIDIOC_S_CTRL, &control2)==-1) {
    cout << "error set sharpness in cam_init" << endl;
    //return(0);
  }

  enum v4l2_buf_type type;
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if(xioctl(videv, VIDIOC_STREAMON, &type)==-1) {
    cout << "error start streaming in cam_init" << endl;
    //return(0);
  }

  cout << "camera streaming started" << endl;

}

int Camera::xioctl(int fd, int request, void *arg) {
  int r;
  do {
    r = ioctl (fd, request, arg);
  } while (-1 == r && EINTR == errno);
  return r;
}

int Camera::status(void) {
  struct v4l2_buffer buf = {0};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  if(xioctl(videv, VIDIOC_QUERYBUF, &buf)==-1) {
    cout << "error query buffer in camStatus" << endl;
    return(0);
  }
  // returns number of frames waiting  
  return(buf.sequence);
}

std::tuple<char*, int> Camera::getFrame(void) {

  struct v4l2_buffer buf = {0};
  
  for (;;) {
    fd_set fds;
    struct timeval tv = {0};
    int r0;
    
    FD_ZERO(&fds);
    FD_SET(videv, &fds);
    
    // set timeout of 2 seconds
    tv.tv_sec = 2;
    r0 = select(videv+1, &fds, NULL, NULL, &tv);
    if(r0 == -1) {
      if (EINTR == errno)
      continue;
      cout << "error select frame in camGetFrame" << endl;
      return {(char*)nullptr, 0};
    }
    if(r0 == 0) {
      cout << "error timeout in camGetFrame" << endl;
      return {(char*)nullptr, 0};
    }
    
    memset(&buf, 0, sizeof(struct v4l2_buffer));
    
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if(xioctl(videv, VIDIOC_DQBUF, &buf) == -1) {
      if (errno == EAGAIN)
      continue;
      cout << "error retrieving frame in camGetFrame" << endl;
      return {(char*)nullptr, 0};
    }
    
    if (buf.index >= N_CAPTURE_BUFS) {
      cout << "error buffer index out of range in camGetFrame" << endl;
      return {(char*)nullptr, 0};
    }
    
    break;
    
  }
  
  if (buffers[buf.index].length > 0) {      
    if(xioctl(videv, VIDIOC_QBUF, &buf) == -1) {
      cout << "error video buffer in camGetFrame" << endl;
      return{(char*)nullptr, 0};
    }

    // cout << "buffers[buf.index=" << buf.index << "].length= " << buffers[buf.index].length << endl;
    
    return { (char*)buffers[buf.index].start, buffers[buf.index].length};
  } else{
    return {(char*)nullptr, 0};
  }
}

class CRC32 {
public:
  CRC32();
  void Reset();
  void Add16bit(uint16_t val);
  uint32_t m_crc;
  uint32_t m_table[256];
};

class CompBuffer {
public:
  CompBuffer(uint8_t *buffer, int bufsize);
  void Reset();
  void Rewind();
  void PushBits(int32_t count, uint32_t bits);
  void EncodeNoChangeCount();
  void AddWord(uint16_t word, uint16_t word_for_crc);
  uint32_t GetBits(int32_t count);
  uint16_t GetWord();
  void Finish();
  uint32_t GetCrc() { return m_crc.m_crc; }
  void AddCrc(uint16_t val) { m_crc.Add16bit(val); }
  bool GetError() { return m_error; }
  uint32_t GetCompressedSize() { return m_compressed_size; }
  uint8_t *GetBuffer() { return m_compressed; }
  void SetBuffer(uint8_t* buffer, int bufsize);
  int32_t GetWordCount() { return m_word_count; }
  uint16_t GetPrevWord(int32_t i) { return m_previous_words[i]; }
  uint8_t cmpbuf0[2] = {0xfd, 0x54}; // { 253, 34 }; for only TXT without servos
protected:
  enum {max_word_count=4096};
  uint16_t m_previous_words[max_word_count];
  uint32_t max_compressed_size;
  uint8_t *m_compressed;
  int32_t m_word_count;
  uint32_t m_compressed_size;
  int32_t m_nochange_count;
  uint32_t m_bitbuffer;
  int32_t m_bitcount;
  CRC32 m_crc;
  bool m_error;
};

CRC32::CRC32() {
  m_crc=0xffffffff;
  for( uint32_t dividend=0; dividend<256; dividend++ ) {
    uint32_t remainder = dividend << 24;
    for ( uint32_t bit = 8; bit > 0; bit--) {
      if (remainder & 0x80000000) {
        remainder = (remainder << 1) ^ 0x04C11DB7;
      } else {
        remainder = (remainder << 1);
      }
    }
    m_table[dividend] = remainder;
  }
}

void CRC32::Reset() {
    m_crc=0xffffffff;
}

void CRC32::Add16bit(uint16_t val) {
    uint8_t data;
    data = (m_crc>>24) ^ (val >> 8);
    m_crc = (m_crc << 8) ^ m_table[data];
    data = (m_crc>>24) ^ (val & 0xff);
    m_crc = (m_crc << 8) ^ m_table[data];
}

CompBuffer::CompBuffer(uint8_t *buffer, int bufsize) : m_compressed(buffer), max_compressed_size(bufsize) {
  Reset();
}

void CompBuffer::Reset() {
  Rewind();
  memset( m_previous_words, 0, sizeof(m_previous_words) );
  memset( m_compressed, 0, sizeof(m_compressed) );
}

void CompBuffer::Rewind() {
  m_word_count = 0;
  m_compressed_size = 0;
  m_nochange_count = 0;
  m_bitcount = 0;
  m_bitbuffer = 0;
  m_crc.Reset();
}

void CompBuffer::SetBuffer(uint8_t* buffer, int bufsize) {
  m_compressed = buffer;
  max_compressed_size = bufsize;
}

void CompBuffer::PushBits( int32_t count, uint32_t bits ) {
  // byte      |2 2 2 2 2 2 2 2|1 1 1 1 1 1 1 1|
  // fragment  |7 7|6 6|5 5|4 4 4 4|3 3|2 2|1 1|                                 
  m_bitbuffer |= (bits << m_bitcount);
  m_bitcount += count;
  while( m_bitcount >= 8 ) {
    m_bitcount -= 8;
    m_compressed[m_compressed_size++] = m_bitbuffer & 0xff;
    m_bitbuffer >>= 8;
  }
}

void CompBuffer::EncodeNoChangeCount() {
  // 00 NoChange 1x16 bit
  // 01 00 NoChange 2x16 bit
  // 01 01 NoChange 3x16 bit
  // 01 10 NoChange 4x16 bit
  // 01 11 xxxx NoChange 5..19x16 bit
  // 01 11 1111 xxxxxxxx NoChange 20..274 x16 bit
  // 01 11 1111 11111111 xxxxxxxx-xxxxxxxx NoChange 275... bit
  while(m_nochange_count) {
    if(m_nochange_count==1) {
      PushBits(2,0);
      break;
    } else 
      if(m_nochange_count<=4) {
        PushBits(2,1);
        PushBits(2,m_nochange_count-2);
        break;
      } else
        if(m_nochange_count<=4+15) {
          PushBits(2,1);
          PushBits(2,3);
          PushBits(4,m_nochange_count-4-1);
          break;
        } else
          if(m_nochange_count<=4+15+255) {
            PushBits(2,1);
            PushBits(2,3);
            PushBits(4,15);
            PushBits(8,m_nochange_count-4-15-1);
            break;
          } else
            if(m_nochange_count<=4+15+255+4096) {
              PushBits(2,1);
              PushBits(2,3);
              PushBits(4,15);
              PushBits(8,255);
              PushBits(16,m_nochange_count-4-15-255-1);
              break;
            } else {
              PushBits(2,1);
              PushBits(2,3);
              PushBits(4,15);
              PushBits(8,255);
              PushBits(16,4095);
              m_nochange_count += -4-15-255-4096;
            }
  }
  m_nochange_count = 0;
}

void CompBuffer::AddWord(uint16_t word, uint16_t word_for_crc) {
  //m_crc.Add16bit(word_for_crc);
  AddCrc(word_for_crc);
  assert( m_word_count < max_word_count );
  // cout << "AddWord(" << word << ") CompBuffer: m_compressed_size= " << m_compressed_size << " max_compressed_size=" << max_compressed_size << endl; 
  assert( m_compressed_size < max_compressed_size-8 );
  if( word == m_previous_words[m_word_count] ) {
    m_nochange_count++;
    m_word_count++;
  } else {
    EncodeNoChangeCount();
    if(word == 1 && m_previous_words[m_word_count]==0 || word == 0 && m_previous_words[m_word_count]!=0) {
      // 10 Toggle (0 to 1, everything else to 0
      PushBits(2,2);
    } else {
      // 11 16 bit follow immediately
      PushBits(2,3);
      PushBits(16,word);
    }
    m_previous_words[m_word_count]=word;
    m_word_count++;
  }
}

uint32_t CompBuffer::GetBits( int32_t count ) {
  // byte      |2 2 2 2 2 2 2 2|1 1 1 1 1 1 1 1|
  // fragment  |7 7|6 6|5 5|4 4 4 4|3 3|2 2|1 1|
  while(m_bitcount<count) {
    m_bitbuffer |= m_compressed[m_compressed_size++] << m_bitcount;
    m_bitcount += 8;
  }
  uint32_t result = m_bitbuffer & (~0U >> (32-count));
  m_bitbuffer >>= count;
  m_bitcount -= count;
  return result;
}

uint16_t CompBuffer::GetWord() {
  assert( m_word_count < max_word_count );
  uint16_t word;
  if( m_nochange_count ) {
    m_nochange_count--;
    word = m_previous_words[m_word_count];
  } else {
    uint32_t head = GetBits(2);
    switch(head) {
      case 0: 
        // 00 NoChange 1x16 bit
        word = m_previous_words[m_word_count];
        break;
      case 1:
        // 01 00 NoChange 2x16 bit
        // 01 01 NoChange 3x16 bit
        // 01 10 NoChange 4x16 bit
        // 01 11 xxxx NoChange 5..19x16 bit
        // 01 11 1111 xxxxxxxx NoChange 20..274 x16 bit
        // 01 11 1111 11111111 xxxxxxxx-xxxxxxxx NoChange 275... x16 bit
        word = m_previous_words[m_word_count];
        {
          uint32_t count = GetBits(2);
          if( count<3 ) {
            m_nochange_count = count+2-1;
          } else {
            count = GetBits(4);
            if( count<15 ) {
              m_nochange_count = count+5-1;
            } else {
              count = GetBits(8);
              if( count<255 ) {
                m_nochange_count = count+20-1;
              } else {
                count = GetBits(16);
                m_nochange_count = count+275-1;
              }
            }
          }
        }
        break;
      case 2:
        // 10 Toggle (0 to 1, everything else to 0
        word = m_previous_words[m_word_count] ? 0 : 1;
        break;
      case 3:
        // 11 16 bit follow immediately
        word = (uint16_t) GetBits(16);
        break;
    }
  }
  m_previous_words[m_word_count++]=word;
  m_crc.Add16bit(word);
  return word;
}

void CompBuffer::Finish() {
  EncodeNoChangeCount();
  if( m_bitcount ) {
    PushBits(8-m_bitcount,0);
  }
}

class TxtConfiguration {
public:
  int16_t config_id;  // incremented when update_config is called
  uint8_t motor[4]; 
  uint8_t input_type[8]; 
  uint8_t input_mode[8];
  uint8_t previous_motor[4]; 
  uint8_t previous_input_type[8]; 
  uint8_t previous_input_mode[8];
  std::shared_ptr<ft::Device> out[8];
  std::shared_ptr<ft::InputDevice> in[8];
  std::shared_ptr<ft::Counter> counter[4];
  bool is_running[4];
  bool previous_is_running[4];
  int16_t running_motor_cmd_id[4];
  int16_t counter_cmd_id[4];
  int16_t sound_cmd_id[4];
};

struct TxtSendDataSimple {
  int32_t m_resp_id;
  int16_t input[8];
  int16_t counter[4];
  int16_t counter_value[4];
  int16_t counter_cmd_id[4];
  int16_t motor_cmd_id[4];
  uint16_t sound_cmd_id;
  int8_t ir[25];
  uint8_t dummy;
};

union TxtSendDataSimpleBuf {
  TxtSendDataSimple txt;
  uint8_t raw[sizeof(TxtSendDataSimple)];
};

struct TxtSendDataCompressed {
  int16_t input[8];
  int16_t counter[4];
  int16_t counter_value[4];
  int16_t counter_cmd_id[4];
  int16_t motor_cmd_id[4];
  uint16_t sound_cmd_id;
  int16_t ir[25];
  uint16_t dummy[2];
};

union TxtSendDataCompressedBuf {
  TxtSendDataCompressed txt;
  uint16_t raw[sizeof(TxtSendDataCompressed)];
};

struct TxtRecvData {
  int16_t pwm[8];
  int16_t motor_sync[4];
  int16_t motor_dist[4];
  int16_t motor_cmd_id[4];
  int16_t counter_cmd_id[4];
  uint16_t sound;
  uint16_t sound_index;
  uint16_t sound_repeat;
  uint16_t dummy;
};

union TxtRecvDataSimpleBuf {
  TxtRecvData txt;
  uint16_t raw[sizeof(TxtRecvData)/2];
};

union TxtRecvDataCompressedBuf {
  TxtRecvData txt;
  uint16_t raw[sizeof(TxtRecvData)/2];
};

void camThread(kn::tcp_socket* camsocket, int width, int height, int framerate) {    
  Camera cam(width, height, framerate);

  {
  kn::socket<(kn::protocol)0> camsock;        
  kn::buffer<1024> cambuf;
  cout << "camera thread started" << endl;
  camsock=camsocket->accept();
  camsock.set_non_blocking(true);
  sleep_for(1500ms);
  while (camera_is_online) {
    if (cam.status()>0) {
      auto [buf, buflen] = cam.getFrame();
      //cout << "camera: " << buflen << " bytes waiting" << endl;
      if (buflen > 0) {
        // cout << "camera: " << buflen << " bytes waiting" << endl;
        uint32_t cam_m_resp_id = 0xBDC2D7A1;
        int framesizecompressed = buflen;
        int framesizeraw = width*height*2;
        int numframesready = 2;
        auto camsendbuf = pystruct::pack(PY_STRING("<Iihhii"), cam_m_resp_id, numframesready, width, height, framesizeraw, framesizecompressed);
        camsock.send(camsendbuf, 20);
        char * bufc = buf;
        int bufcount = buflen;
        while (bufcount > 0) {
          sleep_for(100us);
          if (bufcount > 1500) {
            camsock.send(bufc, 1500);
            bufcount -= 1500;
            bufc += 1500;
          }
          else {
            camsock.send(bufc, bufcount);
            bufcount = 0;
          }
        }
        sleep_for(1ms);
        //camsock.set_non_blocking(false);
        auto [size, valid] = camsock.recv(cambuf);
        uint32_t cam_m_id = (uint8_t)cambuf[0] | (uint8_t)cambuf[1] << 8 | (uint8_t)cambuf[2] << 16 | (uint8_t)cambuf[3] << 24;
        if (cam_m_id == 0xADA09FBA) {
          // cout << "received ACK for camera picture" << endl;
        } 
        else {
          cout << "unknown ACK for camera picture received, cam_m_id = " << std::hex << cam_m_id << endl;
        }          
      }
    } else {
      // cout << "camera not ready" << endl;
    }
  }
  cout << "camera thread finished." << endl;
  } // camsock destructor is called
}

void startI2C(kn::tcp_socket* i2c_socket) {
  //cout << "I2C thread started" << endl;
  //cout << "I2C socket opened ... waiting for connection on port 65002" << endl;
  { // i2csock block
    auto i2csock = i2c_socket->accept();
    // cout << "I2C connection established ... (i2c functionality not yet implemented)" << endl;
    while (i2c_is_online) {
      sleep_for(50ms);
    }
  } // i2csock destructor is called
  //cout << "I2C thread finished." << endl;
};

template<typename T> struct TD;
// to get compiler deduced type of auto var
//    auto [var] = foo();
//    TD<decltype(var)> td;


int main(int argc, char* argv[]) {
  cout << "ftrobopy and ROBOPro Online Server, version " << ftrobopy_server_version 
       << ", (c) 2022 by Torsten Stuehn" << endl;

  TxtConfiguration txt_conf[MAXNUMTXTS] = {};

  TxtSendDataCompressedBuf uncbuf[MAXNUMTXTS] = {};
  TxtSendDataCompressedBuf previous_uncbuf[MAXNUMTXTS] = {};

  TxtRecvDataCompressedBuf recv_uncbuf[MAXNUMTXTS] = {};
  TxtRecvDataCompressedBuf previous_recv_uncbuf[MAXNUMTXTS] = {};

  TxtSendDataSimpleBuf simple_sendbuf = {};
  TxtRecvDataSimpleBuf simple_recvbuf = {};
  TxtRecvDataSimpleBuf previous_simple_recvbuf = {};

  uint32_t crc0 = 0x628ebb05; // 0x0bbf0714 for one TXT without servos
  uint32_t crc = crc0;
  uint32_t previous_crc = crc0;
  uint32_t previous_crc0 = crc0;

  uint32_t recv_crc0 = 0x3040c10f;
  uint32_t recv_crc = recv_crc0;
  uint32_t previous_recv_crc = recv_crc0;
  uint32_t previous_recv_crc0 = recv_crc0;

  unsigned m_id;
  unsigned previous_m_id;
  unsigned m_resp_id;
  bool TransferDataChanged = false;
  bool FirstTransferAfterStop = true;

  //close program upon ctrl+c or other signals
	std::signal(SIGINT, [](int) {
		cout << "Got sigint signal ..." << endl; 
    camera_is_online = false;
    i2c_is_online = false;
    sleep_for(500ms);
    cout << "all running threads closed." << endl;
    main_is_running = false;
    connected = false;
    sleep_for(100ms);
		std::exit(0);
	});
  
  // ignore the SIGPIPE signal (or else the program will exit)
  std::signal(SIGPIPE, SIG_IGN);

  //Send the SIGINT signal to server
	std::thread run_th([] {
		cout << "press return to close ftrobopy_server..." << endl;
		std::cin.get(); //This call only returns when user hits RETURN
		std::cin.clear();
		std::raise(SIGINT);
	});
  run_th.detach();  

  kn::buffer<1024> recvbuf;
  kn::port_t port = 65000; // standard port for ftrobopy communication

  { // start of block containing open sockets

  kn::tcp_socket listen_socket({ "0.0.0.0", 65000 });
  kn::tcp_socket camsocket({ "0.0.0.0", 65001 }); 
  kn::tcp_socket i2c_socket({ "0.0.0.0", 65002 }); 

  try {
    listen_socket.bind();
  } catch (const std::exception& e) {
    cout << "main socket (port 65000) is still in TIME_WAIT - please wait 2 minutes and try again." << endl;
    std::exit(1);
  }
   
  try {
    i2c_socket.bind();
  } catch (const std::exception& e) {
      cout << "i2c socket (port 65002) is still in TIME_WAIT - please wait 2 minutes and try again." << endl;
  std::exit(1);
  }

  try {
    camsocket.bind();
  } catch (const std::exception& e) {
      cout << "camera socket (port 65001) is still in TIME_WAIT - please wait 2 minutes and try again." << endl;
    std::exit(1);
  }
 
  listen_socket.listen();
  listen_socket.set_non_blocking(true);
  i2c_socket.listen();
  i2c_socket.set_non_blocking(true);
  camsocket.listen();
  camsocket.set_non_blocking(true);

  //kn::socket<(kn::protocol)0> sock;
  // to get compiler deduced type of auto var
  // template<typename T> struct TD;
  // TD<decltype(var)> td;

  kn::socket<(kn::protocol)0> sock;
 
  ft::TXT txt("auto");

  main_is_running = true;

  bool valid_connection_established = false;
  bool just_started = true;

  while(main_is_running) {
    crc0 = 0x0bbf0714;
    crc = crc0;
    previous_crc = crc0;
    previous_crc0 = crc0;

    recv_crc0 = 0x3040c10f;
    recv_crc = recv_crc0;
    previous_recv_crc = recv_crc0;
    previous_recv_crc0 = recv_crc0;

    std::memset(uncbuf, 0, sizeof uncbuf);
    std::memset(previous_uncbuf, 0, sizeof previous_uncbuf);
    std::memset(recv_uncbuf, 0, sizeof recv_uncbuf);
    //std::memset(previous_recv_uncbuf, 0, sizeof previous_recv_uncbuf);
    std::memset(&simple_sendbuf, 0, sizeof simple_sendbuf);
    std::memset(&simple_recvbuf, 0, sizeof simple_recvbuf);

    int num_txts = 2;

    if (valid_connection_established || just_started) {
      valid_connection_established = false;
      // k = 0 is TXT Master
      { int k = 0;
        for (int i=0; i<4; i++) {
          txt_conf[k].motor[i]        = txt_conf[k].previous_motor[i] = 1; // motor M
          if (!just_started) {
            txt_conf[k].out[2*i].reset();
            txt_conf[k].out[2*i+1].reset();
          }
          txt_conf[k].out[2*i]        = std::make_unique<ft::Encoder>(txt,i+1);
          std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->startDistance(0,0);
          std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->startSpeed(0);
        }
        for (int i=0; i<8; i++) {
          txt_conf[k].input_type[i]   = txt_conf[k].previous_input_type[i] = 1; // switch
          txt_conf[k].input_mode[i]   = txt_conf[k].previous_input_mode[i] = 1; // digital
          if (!just_started) {
            txt_conf[k].in[i].reset();
          }
          txt_conf[k].in[i]           = std::make_unique<ft::Switch>(txt, i+1);
        }
        for (int i=0; i<4; i++) {
          if (!just_started) {
            txt_conf[k].counter[i].reset();
          }
          txt_conf[k].counter[i]      = std::make_unique<ft::Counter>(txt, i+1);
        }
      }
      // k = 1 is TXT first Slave, currently this is used to control the servos of the TXT 4.0
      { int k = 1;
        for (int i=0; i<3; i++) {
          txt_conf[k].motor[i]        = txt_conf[k].previous_motor[i] = 0; // Servos
          if (!just_started) {
            txt_conf[k].out[i].reset();
          }
          txt_conf[k].out[i]          = std::make_unique<ft::Servo>(txt,i+1);
          std::static_pointer_cast<ft::Servo>(txt_conf[k].out[i])->setPwm(256);
        }
      }
      just_started = false;
      txt.update_config();
    }

    { // open block for sock 

    sock = listen_socket.accept();

    m_id = 0;
    previous_m_id = 0;
    connected = true;
    FirstTransferAfterStop = true;
    while (connected) {    
        sleep_for(8ms);
        auto [size, valid] = sock.recv(recvbuf);

        if (!valid) {
          i2c_is_online = false;
          camera_is_online = false;
          connected = false;
          continue;
        }
        if (size == 0) {
          continue;
        }

        valid_connection_established = true;

        //cout << std::dec << size << " bytes received: ";
        //for (int k=0; k<size; k++) {
        //  cout << std::hex << (int)recvbuf[k] << " ";
        //}
        //cout << endl;

        previous_m_id = m_id;
        m_id = (uint8_t)recvbuf[0] | (uint8_t)recvbuf[1] << 8 | (uint8_t)recvbuf[2] << 16 | (uint8_t)recvbuf[3] << 24;
        switch (m_id) {
          case 0xDC21219A: { // query status
            cout << "got: query status" << endl;
            m_resp_id = 0xBAC9723E;
            auto sendbuf = pystruct::pack(PY_STRING("<I16sI"), m_resp_id, m_devicename, m_version);
            //cout << "  --> sending back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
            //cout << "sendbuf[0-24] : ";
            //for (int i=0; i<24; i++) {
            //  cout << std::hex << (int)sendbuf[i] << " ";
            //}
            //cout << endl;
            sock.send(sendbuf, sendbuf.size());
            break;
          }
          case 0x163FF61D: { // start online
            cout << "got: start online" << endl;
            m_resp_id  = 0xCA689F75;
            auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
            sock.send(sendbuf, sendbuf.size());
            //cout << "  --> sent back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
            i2c_is_online = true;
            std::thread i2c_thread(startI2C, &i2c_socket);
            i2c_thread.detach();
            break;
          }
          case 0x9BE5082C: { // stop online
            cout << "got: stop online" << endl;
            m_resp_id  = 0xFBF600D2;
            auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
            sock.send(sendbuf, sendbuf.size());
            if (previous_m_id != 0xDC21219A) { // ftScratchTXT sends "stop online" immediately after "query status", catch this case here
              i2c_is_online = false;
              camera_is_online = false;
              connected = false;
            }
            break;
          }
          case 0x060EF27E: { // update config
            //cout << "got: update config: ";
            //cout << "recvbuf[0-60] : ";
            /*
            for (int i=0; i<60; i++) {
              cout << std::hex << (int)recvbuf[i] << " ";
            }
            cout << endl;
            */
            m_resp_id  = 0x9689A68C;
            auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
            sock.send(sendbuf, sendbuf.size());

            // "<Ihh B B 2s BBBB BB2s BB2s BB2s BB2s BB2s BB2s BB2s BB2s B3s B3s B3s B3s 16h"
            int txt_nr = (int16_t)recvbuf[6];
            txt_conf[txt_nr].config_id = (int16_t)recvbuf[4];
            // if (txt_nr > 0) { cout << "got update_config for txt-ext nr. " << txt_nr << endl; }
            if (txt_nr == 0) {
              //cout << "update config Txt[" << txt_nr << "]Configuration ConfigID=" << std::dec << (int)txt_conf[txt_nr].config_id << endl;
              for (int i=0; i<4; i++) {
                txt_conf[txt_nr].motor[i] = (uint8_t)recvbuf[12+i];
                //cout << "Txt[" << txt_nr << "]Configuration Motor[" << i << "]=" << (int)txt_conf[txt_nr].motor[i] << endl; 
                if (txt_conf[txt_nr].motor[i] != txt_conf[txt_nr].previous_motor[i]) {
                  if (txt_conf[txt_nr].previous_motor[i] != 0) {
                    //cout << "out[" << i*2 << "] = Lamp";
                    //cout << "out[" << i*2+1 << "] = Lamp";
                    std::static_pointer_cast<ft::MotorDevice>(txt_conf[txt_nr].out[i*2])->stop();
                    txt_conf[txt_nr].out[i*2].reset();
                    txt_conf[txt_nr].out[i*2+1].reset();
                    txt_conf[txt_nr].out[i*2] = std::make_unique<ft::Lamp>(txt, i*2+1 );
                    txt_conf[txt_nr].out[i*2+1] = std::make_unique<ft::Lamp>(txt, i*2+1+1);
                    std::static_pointer_cast<ft::Lamp>(txt_conf[txt_nr].out[i*2])->setBrightness(0);
                    std::static_pointer_cast<ft::Lamp>(txt_conf[txt_nr].out[i*2+1])->setBrightness(0);
                    txt_conf[txt_nr].counter[i]->reset();
                  } else {
                    //cout << "out[" << i*2 << "] = Encoder";
                    txt_conf[txt_nr].out[i*2].reset();
                    txt_conf[txt_nr].out[i*2+1].reset();
                    txt_conf[txt_nr].out[i*2] = std::make_unique<ft::Encoder>(txt, i*2+1);
                    std::static_pointer_cast<ft::Encoder>(txt_conf[txt_nr].out[i*2])->startDistance(0,0);
                    std::static_pointer_cast<ft::Encoder>(txt_conf[txt_nr].out[i*2])->startSpeed(0);
                    txt_conf[txt_nr].counter[i]->reset();
                    txt_conf[txt_nr].is_running[i] = false;
                    txt_conf[txt_nr].previous_is_running[i] = false;
                  }
                  txt_conf[txt_nr].previous_motor[i] = txt_conf[txt_nr].motor[i];
                }
              }
              for (int i=0; i<8; i++) {
                txt_conf[txt_nr].input_type[i] = (uint8_t)recvbuf[16+i*4];
                txt_conf[txt_nr].input_mode[i] = (uint8_t)recvbuf[16+i*4+1];
                //cout << "Txt[" << txt_nr << "]Configuration Input[" << i << "].type=" << (int)txt_conf[txt_nr].input_type[i] << " , Input[" << i << "].mode=" << (int)txt_conf[txt_nr].input_mode[i] << endl;
                if (txt_conf[txt_nr].input_type[i] != txt_conf[txt_nr].previous_input_type[i] ||
                    txt_conf[txt_nr].input_mode[i] != txt_conf[txt_nr].input_mode[i]) {

                  txt_conf[txt_nr].in[i].reset();

                  if (txt_conf[txt_nr].input_mode[i] != 0) {  // digital input
                    switch (txt_conf[txt_nr].input_type[i]) {
                      case 0:
                        txt_conf[txt_nr].in[i] = std::make_unique<ft::TrailFollower>(txt, i+1); 
                        //cout << "set input I" << i+1 << " = Trailfollower" << endl;
                        break;
                      case 1:
                        txt_conf[txt_nr].in[i] = std::make_unique<ft::Switch>(txt, i+1); 
                        //cout << "set input I" << i+1 << " = Switch" << endl;
                        break;
                      default:
                        //cout << "error: unknown digital input type " << txt_conf[txt_nr].input_type[i] << endl;
                        txt_conf[txt_nr].in[i] = std::make_unique<ft::Switch>(txt, i+1); 
                      break;
                    }
                  } else { // analog input
                    switch (txt_conf[txt_nr].input_type[i]) {
                      case 0:
                        txt_conf[txt_nr].in[i] = std::make_unique<ft::Voltmeter>(txt, i+1);
                        //cout << "set input I" << i+1 << " = Voltmeter" << endl;
                        break;
                      case 1:
                        txt_conf[txt_nr].in[i] = std::make_unique<ft::Resistor>(txt, i+1);
                        //cout << "set input I" << i+1 << " = Resistor" << endl;
                        break;
                      case 3:
                        txt_conf[txt_nr].in[i] = std::make_unique<ft::Ultrasonic>(txt, i+1);
                        //cout << "set input I" << i+1 << " = Ultrasonic" << endl;
                        //cout << "txt_conf[" << txt_nr << "].in[" << i << "] = Ultrasonic" << endl;
                        break;
                      default:
                        cout << "error: unknown digital input type " << txt_conf[txt_nr].input_type[i] << endl;
                        txt_conf[txt_nr].in[i] = std::make_unique<ft::Voltmeter>(txt, i+1); 
                      break;
                    }
                  }
                  txt_conf[txt_nr].previous_input_type[i] = txt_conf[txt_nr].input_type[i];
                  txt_conf[txt_nr].previous_input_mode[i] = txt_conf[txt_nr].input_mode[i];
                }
              }
              txt.update_config();
            }
            else if (txt_nr == 1) {
              for (int i=0; i<3; i++) {
                std::static_pointer_cast<ft::Servo>(txt_conf[txt_nr].out[i])->setPwm(256); // Servo
              }
            }
            break;
          }
          case 0xCC3597BA: {
            //cout << "got: exchange data simple" << endl;
            m_resp_id = 0x4EEFAC41;
            //cout << "recvbuf[0-60] : ";
            //for (int i=0; i<60; i++) {
            //  cout << std::hex << (int)recvbuf[i] << " ";
            //}
            //cout << endl;

            /////////////////////////////
            // prepare simple send buffer
            /////////////////////////////

            simple_sendbuf.txt.m_resp_id = m_resp_id;
            for (int i=0; i<8; i++) {

                  if (txt_conf[0].input_mode[i] != 0) {  // digital input
                    switch (txt_conf[0].input_type[i]) {
                      case 0:
                        simple_sendbuf.txt.input[i] = txt_conf[0].in[i]->getState(); // ft::Trailfollower
                        break;
                      case 1:
                        simple_sendbuf.txt.input[i] = txt_conf[0].in[i]->getState(); // ft::Switch
                        break;
                      default:
                        simple_sendbuf.txt.input[i] = txt_conf[0].in[i]->getState(); // ft::InputDevice
                      break;
                    }
                  } else { // analog input
                    switch (txt_conf[0].input_type[i]) {
                      case 0:
                        simple_sendbuf.txt.input[i] = std::static_pointer_cast<ft::Voltmeter>(txt_conf[0].in[i])->getVoltage(); // ft::Voltmeter
                        break;
                      case 1:
                        simple_sendbuf.txt.input[i] = std::static_pointer_cast<ft::Resistor>(txt_conf[0].in[i])->getResistance(); // ft::Resistor
                        break;
                      case 3:
                        simple_sendbuf.txt.input[i] = std::static_pointer_cast<ft::Ultrasonic>(txt_conf[0].in[i])->getDistance(); // ft::Ultrasonic
                        break;
                      default:
                        simple_sendbuf.txt.input[i] = txt_conf[0].in[i]->getState(); // ft::InputDevice
                      break;
                    }
                  }
            }
            for (int i=0; i<4; i++) simple_sendbuf.txt.counter_value[i] = txt_conf[0].counter[i]->getDistance();
            for (int i=0; i<4; i++) simple_sendbuf.txt.counter[i] = txt_conf[0].counter[i]->getState();
            for (int i=0; i<4; i++) simple_sendbuf.txt.counter_cmd_id[i] = txt_conf[0].counter_cmd_id[i];
            for (int i=0; i<4; i++) {
              if (txt_conf[0].motor[i] == 1) {
                txt_conf[0].previous_is_running[i] = txt_conf[0].is_running[i];
                txt_conf[0].is_running[i] =  std::static_pointer_cast<ft::Encoder>(txt_conf[0].out[2*i])->isRunning();
                if (txt_conf[0].previous_is_running[i] && !txt_conf[0].is_running[i]) {
                  simple_sendbuf.txt.motor_cmd_id[i] = txt_conf[0].running_motor_cmd_id[i];
                  //cout << "new motor_cmd_id=" << simple_sendbuf.txt.motor_cmd_id[i] << endl;
                } 
              }
            }
            simple_sendbuf.txt.sound_cmd_id = 0;
            for (int i=0; i<4; i++) simple_sendbuf.txt.ir[i] = 0;
            simple_sendbuf.txt.dummy = 0;

            //cout << "  --> sending back " << std::dec << sizeof(TxtSendDataSimple) << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
            {
              std::array<char, sizeof(TxtSendDataSimple)> sendbuf;
              for (int i=0; i<sizeof(TxtSendDataSimple); i++) {
                //cout << std::dec << i << " ";
                sendbuf[i]=simple_sendbuf.raw[i];
              }
              sock.send(sendbuf, sendbuf.size());
            }

            /////////////////////////////
            // prepare simple recv buffer
            /////////////////////////////
            // pwm
            for (int i=0; i<4; i++) {
              simple_recvbuf.txt.pwm[2*i]   = (uint8_t)recvbuf[2*(2*i)  +4] | (uint8_t)recvbuf[2*(2*i)  +5]<<8;
              simple_recvbuf.txt.pwm[2*i+1] = (uint8_t)recvbuf[2*(2*i+1)+4] | (uint8_t)recvbuf[2*(2*i+1)+5]<<8;
              if ((simple_recvbuf.txt.pwm[2*i]   != previous_simple_recvbuf.txt.pwm[2*i]) || 
                  (simple_recvbuf.txt.pwm[2*i+1] != previous_simple_recvbuf.txt.pwm[2*i+1])) {
                previous_simple_recvbuf.txt.pwm[2*i]   = simple_recvbuf.txt.pwm[2*i];
                previous_simple_recvbuf.txt.pwm[2*i+1] = simple_recvbuf.txt.pwm[2*i+1];
                if (txt_conf[0].motor[i] == 1) {
                  int speed = simple_recvbuf.txt.pwm[2*i] - simple_recvbuf.txt.pwm[2*i+1];
                  std::static_pointer_cast<ft::Encoder>(txt_conf[0].out[2*i])->startSpeed(speed);  // ft::Encoder(txt,i+1)
                } else {
                  std::static_pointer_cast<ft::Lamp>(txt_conf[0].out[2*i])->setBrightness(simple_recvbuf.txt.pwm[2*i]);
                  std::static_pointer_cast<ft::Lamp>(txt_conf[0].out[2*i+1])->setBrightness(simple_recvbuf.txt.pwm[2*i+1]);
                }
              }
            }
            // encoder
            for (int i=0; i<4; i++) {
              simple_recvbuf.txt.motor_sync[i] = (uint8_t)recvbuf[2*i+20] | (uint8_t)recvbuf[2*i+21]<<8;
              simple_recvbuf.txt.motor_dist[i] = (uint8_t)recvbuf[2*i+28] | (uint8_t)recvbuf[2*i+29]<<8;
              simple_recvbuf.txt.motor_cmd_id[i] = (uint8_t)recvbuf[2*i+36] | (uint8_t)recvbuf[2*i+37]<<8;
              //cout << "sync[" << i << "] = " << simple_recvbuf.txt.motor_sync[i] << " ";
              //cout << "dist[" << i << "] = " << simple_recvbuf.txt.motor_dist[i] << " ";
              //cout << "M_cmd_id[" << i << "] = " << simple_recvbuf.txt.motor_cmd_id[i] << endl;
              if (simple_recvbuf.txt.motor_cmd_id[i] != previous_simple_recvbuf.txt.motor_cmd_id[i]) {
                //cout << "motor_cmd_id increased in simple_recv" << endl;
                //cout << "speed[" << i << "] = " << (simple_recvbuf.txt.pwm[2*i] - simple_recvbuf.txt.pwm[2*i+1]) << " ";
                //cout << "sync[" << i << "] = " << simple_recvbuf.txt.motor_sync[i] << " ";
                //cout << "dist[" << i << "] = " << simple_recvbuf.txt.motor_dist[i] << " ";
                //cout << "M_cmd_id[" << i << "] = " << simple_recvbuf.txt.motor_cmd_id[i] << " ";
                previous_simple_recvbuf.txt.motor_cmd_id[i] = simple_recvbuf.txt.motor_cmd_id[i];
                txt_conf[0].running_motor_cmd_id[i] = simple_recvbuf.txt.motor_cmd_id[i];
                //std::static_pointer_cast<ft::Counter>(txt_conf[0].counter[i])->reset();
                if (simple_recvbuf.txt.motor_sync[i] == 0) {
                  std::static_pointer_cast<ft::Encoder>(txt_conf[0].out[2*i])->
                              startDistance(simple_recvbuf.txt.motor_dist[i], 0);
                } else {
                  std::static_pointer_cast<ft::Encoder>(txt_conf[0].out[2*i])->
                       startDistance(simple_recvbuf.txt.motor_dist[i],
                          std::static_pointer_cast<ft::Encoder>(txt_conf[0].out[2*(simple_recvbuf.txt.motor_sync[i]-1)]).get());
                }
                txt_conf[0].is_running[i] = std::static_pointer_cast<ft::Encoder>(txt_conf[0].out[2*i])->isRunning();
              }
            }
            // counter
            for (int i=0; i<4; i++) {
              simple_recvbuf.txt.counter_cmd_id[i] = (uint8_t)recvbuf[2*i+44] | (uint8_t)recvbuf[2*i+45]<<8;
              if (simple_recvbuf.txt.counter_cmd_id[i] != previous_simple_recvbuf.txt.counter_cmd_id[i]) {
                previous_simple_recvbuf.txt.counter_cmd_id[i] = simple_recvbuf.txt.counter_cmd_id[i];
                std::static_pointer_cast<ft::Counter>(txt_conf[0].counter[i])->reset();
                txt_conf[0].counter_cmd_id[i] = simple_recvbuf.txt.counter_cmd_id[i];
              }
            }
            break;
          }        
          case  0xFBC56F98: {
            // cout << "got: exchange data compressed" << endl;
            m_resp_id = 0x6F3B54E6;
            unsigned m_extrasize;
            unsigned recv_m_extrasize;
            uint8_t recv_buf[1024];
            uint8_t send_buf[1024];
            CompBuffer recv_compbuf(recv_buf, 1024);
            CompBuffer send_compbuf(send_buf, 1024);
            uint8_t* send_body;

            recv_m_extrasize = (uint8_t)recvbuf[4] | (uint8_t)recvbuf[5] << 8 | (uint8_t)recvbuf[6] << 16 | (uint8_t)recvbuf[7] << 24;
            //cout << "receive buffer extrasize = " << recv_m_extrasize << endl;
            recv_crc = (uint8_t)recvbuf[8] | (uint8_t)recvbuf[9] << 8 | (uint8_t)recvbuf[10] << 16 | (uint8_t)recvbuf[11] << 24;
            //cout << "receive buffer crc = " << recv_crc << endl; // 0x40493a53

            /////////////////////////////
            // uncompress receive buffer
            /////////////////////////////
            if (recv_crc != previous_recv_crc) {
              previous_recv_crc = recv_crc;
              //cout << "got: new exchange data compressed" << endl;
              //cout << "receive buffer extrasize = " << std::dec << recv_m_extrasize << endl;
              //cout << "receive buffer crc = " << std::hex << recv_crc << endl; // 0x40493a53
              //for (int i=0; i<recv_m_extrasize; i++) {
              //  recv_buf[i] = recvbuf[i+16];
              //  cout << std::hex << (int)recv_buf[i] << " ";
              //}
              //cout << endl;
              memcpy(previous_recv_uncbuf, recv_uncbuf, sizeof(TxtRecvDataCompressedBuf)*num_txts);              
              recv_compbuf.Reset();
              recv_compbuf.SetBuffer((uint8_t*)(recvbuf.data())+16,1024-16);
              //cout << "received      : ";
              for (int k=0; k<num_txts; k++) {
                for (int i=0; i<(sizeof(TxtRecvData)-1)/2; i++) {
                  int w = recv_compbuf.GetWord();
                  int z = previous_recv_uncbuf[k].raw[i];
                  if (w==0) recv_uncbuf[k].raw[i]=z;
                  else
                    if (w==1 && z==1) recv_uncbuf[k].raw[i]=0;
                    else
                      if (w==1 && z==0) recv_uncbuf[k].raw[i]=1;
                      else recv_uncbuf[k].raw[i]=w;
                  //cout << std::hex << recv_uncbuf[k].raw[i] << " " << std::flush; 
                } 
              }  
              //cout << endl;

              /*
              if (recv_crc != recv_compbuf.GetCrc()) {
                //cout << "ERROR: received CRC does not match calculated CRC!" << endl;
                //cout << "       transmitted crc = " << recv_crc <<"  calculated crc = " << recv_compbuf.GetCrc() << endl;
                cout << "crc ok" << endl;
              } else {
                cout << "bad crc" << endl;
              }
              */

              //previous_recv_crc = recv_compbuf.GetCrc(); // before:recv_crc;

              { int k = 0;  //for (int k=0; k<num_txts; k++)

                // pwm
                for (int i=0; i<4; i++) {
                  if ((recv_uncbuf[k].txt.pwm[2*i] != previous_recv_uncbuf[k].txt.pwm[2*i]) ||
                      (recv_uncbuf[k].txt.pwm[2*i+1] != previous_recv_uncbuf[k].txt.pwm[2*i+1])) {
                    if (txt_conf[k].motor[i] == 1) {
                      int speed = recv_uncbuf[k].txt.pwm[2*i] - (recv_uncbuf[k].txt.pwm[2*i+1]);
                      std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->startSpeed(speed);     // ft::Encoder(txt,i+1)
                    } else {
                      std::static_pointer_cast<ft::Lamp>(txt_conf[k].out[2*i])->setBrightness(recv_uncbuf[k].txt.pwm[i]);
                      std::static_pointer_cast<ft::Lamp>(txt_conf[k].out[2*i+1])->setBrightness(recv_uncbuf[k].txt.pwm[i]);
                    }
                  }
                }

                // encoder distance
                for (int i=0; i<4; i++) {
                  if (recv_uncbuf[k].txt.motor_cmd_id[i] != previous_recv_uncbuf[k].txt.motor_cmd_id[i]) {
                    txt_conf[k].running_motor_cmd_id[i] = recv_uncbuf[k].txt.motor_cmd_id[i];
                    //std::static_pointer_cast<ft::Counter>(txt_conf[k].counter[i])->reset();
                    //uncbuf[k].txt.counter_cmd_id[i] = recv_uncbuf[k].txt.counter_cmd_id[i];
                    if (recv_uncbuf[k].txt.motor_sync[i] == 0) {
                      std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->
                                  startDistance(recv_uncbuf[k].txt.motor_dist[i], 0);
                    } else {
                        std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->
                             startDistance(recv_uncbuf[k].txt.motor_dist[i],
                                std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*(recv_uncbuf[k].txt.motor_sync[i]-1)]).get()
                             );
                    }
                    txt_conf[k].is_running[i] = std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->isRunning();
                  }
                }

                // counter
                for (int i=0; i<4; i++) {
                  if (recv_uncbuf[k].txt.counter_cmd_id[i] != previous_recv_uncbuf[k].txt.counter_cmd_id[i]) {
                    std::static_pointer_cast<ft::Counter>(txt_conf[k].counter[i])->reset();
                    uncbuf[k].txt.counter_cmd_id[i] = recv_uncbuf[k].txt.counter_cmd_id[i];
                    txt_conf[k].counter_cmd_id[i] = uncbuf[k].txt.counter_cmd_id[i];
                  }
                }
              }

              { int k = 1;
                // servo pwm
                for (int i=0; i<3; i++) {
                  if (recv_uncbuf[k].txt.pwm[i] != previous_recv_uncbuf[k].txt.pwm[i]) {
                    std::static_pointer_cast<ft::Servo>(txt_conf[k].out[i])->setPwm(recv_uncbuf[k].txt.pwm[i]);
                  }
                }
              }
            }

            /////////////////////////////
            // prepare send buffer
            /////////////////////////////            
            std::memset(uncbuf, 0, sizeof uncbuf);
            { int k = 0;
              for (int i=0; i<8; i++) {
                  if (txt_conf[k].input_mode[i] != 0) {  // digital input
                    switch (txt_conf[k].input_type[i]) {
                      case 0:
                        uncbuf[k].txt.input[i] = txt_conf[k].in[i]->getState(); // ft::Trailfollower
                        //cout << "Input I" << i << " is Trailfollower (digital) = " << uncbuf[k].txt.input[i] << endl;
                        break;
                      case 1:
                        uncbuf[k].txt.input[i] = txt_conf[k].in[i]->getState(); // ft::Switch
                        //cout << "Input I" << i << " is Switch (digital) = " << uncbuf[k].txt.input[i] << endl;
                        break;
                      default:
                        uncbuf[k].txt.input[i] = txt_conf[k].in[i]->getState(); // ft::InputDevice
                        //cout << "Input I" << i << " is default InputDevice (digital) = " << uncbuf[k].txt.input[i] << endl;
                      break;
                    }
                  } else { // analog input
                    switch (txt_conf[k].input_type[i]) {
                      case 0:
                        uncbuf[k].txt.input[i] = std::static_pointer_cast<ft::Voltmeter>(txt_conf[k].in[i])->getVoltage(); // ft::Voltmeter
                        //cout << "Input I" << i << " is Voltmeter (analog) = " << uncbuf[k].txt.input[i] << endl;
                        break;
                      case 1:
                        uncbuf[k].txt.input[i] = std::static_pointer_cast<ft::Resistor>(txt_conf[k].in[i])->getResistance(); // ft::Resistor
                        //cout << "Input I" << i << " is Resistor (analog) = " << uncbuf[k].txt.input[i] << endl;
                        break;
                      case 3:
                        uncbuf[k].txt.input[i] = std::static_pointer_cast<ft::Ultrasonic>(txt_conf[k].in[i])->getDistance(); // ft::Ultrasonic
                        //cout << "Input I" << i << " is Ultrasonic (analog) = " << uncbuf[k].txt.input[i] << endl;
                        break;
                      default:
                        uncbuf[k].txt.input[i] = txt_conf[k].in[i]->getState(); // ft::InputDevice
                        //cout << "Input I" << i << " is default InputDevice (analog) = " << uncbuf[k].txt.input[i] << endl;
                      break;
                    }
                  }

                if (uncbuf[k].txt.input[i] != previous_uncbuf[k].txt.input[i]) {
                  TransferDataChanged = true;
                }
              }
              for (int i=0; i<4; i++) {
                uncbuf[k].txt.counter_value[i] = txt_conf[k].counter[i]->getDistance();
                if (uncbuf[k].txt.counter_value[i] != previous_uncbuf[k].txt.counter_value[i]) {
                  TransferDataChanged = true;
                }
              }
              for (int i=0; i<4; i++) {
                uncbuf[k].txt.counter[i] = txt_conf[k].counter[i]->getState();
                if (uncbuf[k].txt.counter[i] != previous_uncbuf[k].txt.counter[i]) {
                  TransferDataChanged = true;
                }
              }
              for (int i=0; i<4; i++) {
                if (txt_conf[k].motor[i] == 1) {
                  txt_conf[k].previous_is_running[i] = txt_conf[k].is_running[i];
                  txt_conf[k].is_running[i] =  std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->isRunning();
                  if (txt_conf[k].previous_is_running[i] && !txt_conf[k].is_running[i]) {
                    uncbuf[k].txt.motor_cmd_id[i] = txt_conf[k].running_motor_cmd_id[i];
                    TransferDataChanged = true;
                  } 
                }
              }
            }
            /*
            { int k = 1;
              for (int i=0; i<8; i++) {
                uncbuf[k].txt.input[i] = 0;
              }
              for (int i=0; i<4; i++) {
                uncbuf[k].txt.counter_value[i] = 0;
                uncbuf[k].txt.counter[i] = 0;
              }
            }
            */
            if (TransferDataChanged || FirstTransferAfterStop) {
              FirstTransferAfterStop = false;
              //cout << "sent exch_data: ";
              send_compbuf.Reset();
              for (int k=0; k<num_txts; k++) {
                // cout << "sizeof(TxtSendDataCompressed) = " << std::dec << sizeof(TxtSendDataCompressed) << endl;
                for (int i=0; i<sizeof(TxtSendDataCompressed)/2; i++) {
                  if (uncbuf[k].raw[i] == previous_uncbuf[k].raw[i]) {
                    //send_compbuf.AddWord(0, uncbuf[k].raw[i]);
                    send_compbuf.AddWord(0, previous_uncbuf[k].raw[i]);
                    //cout << std::hex << 0 << " ";
                  } else {
                      if (uncbuf[k].raw[i] == 0) {
                        send_compbuf.AddWord(1, 0);
                        //cout << std::hex << 1 << " ";
                      } else {
                        send_compbuf.AddWord(uncbuf[k].raw[i], uncbuf[k].raw[i]);
                        //cout << std::hex << uncbuf[k].raw[i] << " ";
                      }
                    }  
                }
              }
              //cout << endl;
              send_compbuf.Finish();
              memcpy(previous_uncbuf, uncbuf, sizeof(TxtSendDataCompressedBuf)*num_txts);
              crc = send_compbuf.GetCrc();
              previous_crc = crc;
              send_body = send_compbuf.GetBuffer();
              m_extrasize = send_compbuf.GetCompressedSize();
              TransferDataChanged = false;
              //cout << "m_extrasize=" << std::dec << m_extrasize << "  CRC=0x" << std::hex << crc << endl;
              //cout << "sent exchdata: "; for (int i=0; i<m_extrasize; i++) cout << std::hex << (int)send_body[i] << " "; cout << endl;
            } else {
              crc = previous_crc;
              send_body =  send_compbuf.cmpbuf0;
              m_extrasize = 2;
              //cout << "sent exch_data: " << std::hex << (int)send_compbuf.cmpbuf0[0] << " " << (int)send_compbuf.cmpbuf0[1] << endl;
              //cout << "sent same, was: "; for (int k=0; k<num_txts; k++) for (int i=0; i<sizeof(TxtSendDataCompressed)/2; i++) cout << std::hex << (int)previous_uncbuf[k].raw[i] << " "; cout << endl;
            }

            auto header = pystruct::pack(PY_STRING("<IIIHH"),
              m_resp_id, 
              m_extrasize,              // ,2 extra buffer size
              crc,                      // 0x0bbf0714, crc32 of extra buffer
              1,                        // number of active extensions
              0);                       // dummy align

            std::array<char, 1024> sendbuf;
            for (int i=0; i<header.size(); i++) sendbuf[i]=header[i];
            for (int i=0; i<m_extrasize; i++) sendbuf[header.size()+i] = send_body[i];

            //cout << "  --> sending back " << std::dec << header.size()+m_extrasize << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
            sock.send(sendbuf, header.size()+m_extrasize);
            break;
          }
          case 0x882A40A6: { // startCameraOnline
            cout << "got: startCameraOnline" << endl;
            m_resp_id  = 0xCF41B24E;
            auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
            sock.send(sendbuf, sendbuf.size());
            auto [dummy_m_id, width, height, framerate, powerlinefreq] = pystruct::unpack(PY_STRING("<I4i"), recvbuf);
            if (!camera_is_online) {
              camera_is_online = true;
              std::thread camthread(camThread, &camsocket, width, height, framerate);
              camthread.detach();
            }
            break;
          }       
          case 0x17C31F2F: { // stopCameraOnline
            cout << "got: stopCameraOnline" << endl;
            m_resp_id  = 0x4B3C1EB6;
            auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
            sock.send(sendbuf, sendbuf.size());
            camera_is_online = false;
            break;
          }   
          case 0xACAB31F7: { // "Stop all running programs" pressed in ROBOPro
            cout << "got: Stop all running programs button pressed in ROBOPro" << endl;
            m_resp_id  = 0x15B1758E;
            auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
            // cout << "  --> sending back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
            sock.send(sendbuf, sendbuf.size());
            connected = false;
            camera_is_online = false;
            i2c_is_online = false;
            break;
          }
          case 0xDAF84364: { // Upload Program to TXT
            cout << "got: Upload Program to TXT" << endl;
            m_resp_id  = 0x5170C53B; // +sendbuf (Speicherlayout) = 00 60 7d b6 00 00 40 00 48 d9 6b b6 00 24 00 00
            // ("kein Speicherlayout erhalten!"-Fehler in ROBOPro, wenn sendbuf leer)
            auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
            // cout << "  --> sending back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
            sock.send(sendbuf, sendbuf.size());
            connected = false;
            break;
          }
          case 0x00000000: { // no data
            break;
          }
          default: {
            cout << "unknown command or command not supported" << endl;
            cout << "m_id=" << std::hex << m_id << endl;
            cout << std::dec << size << " bytes received: ";
            for (int k=0; k<size; k++) {
            cout << std::hex << (int)recvbuf[k] << " ";
            }
            cout << endl;     
            m_resp_id = 0x0;
            auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
            cout << "  --> sending back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
            sock.send(sendbuf, sendbuf.size());
            connected = false;
            break;
          }
        } 
    } // while(connected)
    } // close block of sock to call its destructor
  } // while(main_is_running)
  listen_socket.shutdown();
  i2c_socket.shutdown();
  camsocket.shutdown();
  sleep_for(100ms);
 } // end of block containing open sockets
 cout << "ftrobopy_server closed." << endl;
 sleep_for(100ms);
} // main
