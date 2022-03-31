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

#define VERSION "0.8.8"

/*
__author__      = "Torsten Stuehn"
__copyright__   = "Copyright 2022 by Torsten Stuehn"
__credits__     = "fischertechnik GmbH"
__license__     = "MIT License"
__version__     = "0.8.9"
__maintainer__  = "Torsten Stuehn"
__email__       = "stuehn@mailbox.org"
__status__      = "alpha"
__date__        = "03/28/2022"
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

#define NUMTXTS 1
#define MAXNUMTXTS 9  // maximum=9 (1 master + 8 slaves)

#define N_CAPTURE_BUFS 2

bool camera_is_online = false;
bool i2c_is_online = false;

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
    //cout << "error stop streaming in camClose" << endl;
    //return(0);
  }
  if (buffers!=NULL) {
    for (int i = 0; i < N_CAPTURE_BUFS; i++) {
      if (munmap(buffers[i].start, buffers[i].length) == -1) {
        //cout << "error unmapping memory buffers in camClose" << endl;
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
  format = V4L2_PIX_FMT_MJPEG; // alternative: V4L2_PIX_FMT_YUYV
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

  enum v4l2_buf_type type;
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if(xioctl(videv, VIDIOC_STREAMON, &type)==-1) {
    cout << "error start streaming in cam_init" << endl;
    //return(0);
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

  // returns the number of the video device
  //return(videv);  
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
    return { (char*)buffers[buf.index].start, buffers[buf.index].length};
  } else{
    return {(char*)nullptr, 0};
  }
}

void camThread(int width, int height, int framerate) {    
  //cout << std::dec << "width=" << width << " height=" << height << " framerate=" << framerate << endl;
  //int videv = camInit(width, height, framerate);
  Camera cam(width, height, framerate);
  //cout << "videv=" << videv << endl;
  sleep_for(100ms);
  kn::tcp_socket camsocket({ "0.0.0.0", 65001 }); 
  kn::socket<(kn::protocol)0> camsock;        
  kn::buffer<1024> cambuf;
  kn::port_t camport = 65001;
  camsocket.bind();
  camsocket.listen();
  camsock=camsocket.accept();
  while (camera_is_online) {
    // if (camStatus(videv)>0) {
    if (cam.status()>0) {
      //auto [buf, buflen] = camGetFrame(videv);
      auto [buf, buflen] = cam.getFrame();
      //cout << "buflen=" << buflen << endl;
      if (buflen > 0) {
        uint32_t cam_m_resp_id = 0xBDC2D7A1;
        int framesizecompressed = buflen;
        int framesizeraw = width*height*2;
        int numframesready = 1;
        auto camsendbuf = pystruct::pack(PY_STRING("<Iihhii"), cam_m_resp_id, numframesready, width, height, framesizeraw, framesizecompressed);
        camsock.send(camsendbuf, 20);
        char * bufc = buf;
        int bufcount = buflen;
        while (bufcount > 0) {
          if (bufcount > 1500) {
            camsock.send(bufc, 1500);
            bufcount -= 1500;
            bufc += 1500;
          }
          else {
            camsock.send(bufc, bufcount);
            bufcount = 0;
          }
          //cout << "bufcount: " << bufcount << endl; 
        }
        auto [size, valid] = camsock.recv(cambuf);
        uint32_t cam_m_id = (uint8_t)cambuf[0] | (uint8_t)cambuf[1] << 8 | (uint8_t)cambuf[2] << 16 | (uint8_t)cambuf[3] << 24;
        if (cam_m_id == 0xADA09FBA) {
          //cout << "received ACK for camera picture" << endl;
        } 
        else {
          cout << "no ACK for camera picture cam_m_id = " << std::hex << cam_m_id << endl;
        }          
      }
    }
    sleep_for(5ms);
  }
  //camClose(videv);  automatic due to RAII pattern of Camera class
  camsock.close();
  camsocket.close();
}

void startI2C() {
  kn::tcp_socket i2c_socket({ "0.0.0.0", 65002 }); 
  kn::buffer<1024> recvbuf;
  i2c_socket.bind();
  i2c_socket.listen();
  auto i2csock = i2c_socket.accept();
  //cout << "I2C socket opened ..." << endl;
  while (i2c_is_online) {
    sleep_for(1000ms);
  }
  i2csock.close();
  i2c_socket.close();
};

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
  uint16_t raw[sizeof(TxtRecvData)];
};

union TxtRecvDataCompressedBuf {
  TxtRecvData txt;
  uint16_t raw[sizeof(TxtRecvData)];
};

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
  bool GetError() { return m_error; }
  uint32_t GetCompressedSize() { return m_compressed_size; }
  uint8_t *GetBuffer() { return m_compressed; }
  void SetBuffer(uint8_t* buffer, int bufsize);
  int32_t GetWordCount() { return m_word_count; }
  uint16_t GetPrevWord(int32_t i) { return m_previous_words[i]; }
  uint8_t cmpbuf0[2] = { 253, 34 };
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
  m_crc.Add16bit(word_for_crc);
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
  //if (word==0) word = m_previous_words[m_word_count];
  //else if (word==1 && m_previous_words[m_word_count]==1) word = 0;
  //     else if (word==1 &&  m_previous_words[m_word_count]==0) word = 1;  
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

kn::tcp_socket listen_socket({ "0.0.0.0", 65000 }); 
kn::socket<(kn::protocol)0> sock;
// to get compiler decuced type of auto var
// template<typename T> struct TD;
// TD<decltype(var)> td;

ft::TXT txt("auto");

int main(int argc, char* argv[]) {

  cout << "ftrobopy and ROBOPro Online Server, version " << ftrobopy_server_version 
       << ", (c) 2022 by Torsten Stuehn" << endl;

  sleep_for(20ms);

  TxtConfiguration txt_conf[MAXNUMTXTS] = {};
  TxtConfiguration previous_txt_conf[MAXNUMTXTS] = {};

  TxtSendDataCompressedBuf uncbuf[MAXNUMTXTS] = {};
  TxtSendDataCompressedBuf previous_uncbuf[MAXNUMTXTS] = {};

  TxtRecvDataCompressedBuf recv_uncbuf[MAXNUMTXTS] = {};
  TxtRecvDataCompressedBuf previous_recv_uncbuf[MAXNUMTXTS] = {};

  TxtSendDataSimpleBuf simple_sendbuf = {};
  TxtRecvDataSimpleBuf simple_recvbuf = {};
  TxtRecvDataSimpleBuf previous_simple_recvbuf = {};

  uint32_t crc0 = 0x0bbf0714;
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

  bool connected;
  kn::buffer<1024> recvbuf;
  kn::port_t port = 65000; // standard port for ftrobopy communication

  if (argc >= 2) { // get port from command line if given
    port = kn::port_t(strtoul(argv[1], nullptr, 10));
  }

  //close program upon ctrl+c or other signals
	std::signal(SIGINT, [](int) {
		cout << "Got sigint signal... closing socket" << endl; 
    camera_is_online = false;
    i2c_is_online = false;
    sock.close();
    listen_socket.close();
    txt.reset();
    sleep_for(100ms);
		std::exit(0);
	});


  std::signal(SIGPIPE, SIG_IGN);

  //catch the SIGPIPE signal (else the program will exit)
   /*
	std::signal(SIGPIPE, [](int) {
		cout << "Got SIGPIPE signal... " << endl; 
		//std::exit(0);
	});  
  */

  //Send the SIGINT signal to ourself if user press return on "server" terminal
	std::thread run_th([] {
		cout << "press return to close ftrobopy_server..." << endl;
		std::cin.get(); //This call only returns when user hit RETURN
		std::cin.clear();
		std::raise(SIGINT);
	});
  run_th.detach();  

  listen_socket.bind();
  listen_socket.listen();
  
  while(true) {

  crc0 = 0x0bbf0714;
  crc = crc0;
  previous_crc = crc0;
  previous_crc0 = crc0;

  recv_crc0 = 0x3040c10f;
  recv_crc = recv_crc0;
  previous_recv_crc = recv_crc0;
  previous_recv_crc0 = recv_crc0;

  std::memset(txt_conf, 0, sizeof txt_conf);
  std::memset(previous_txt_conf, 0, sizeof previous_txt_conf);
  std::memset(uncbuf, 0, sizeof uncbuf);
  std::memset(previous_uncbuf, 0, sizeof previous_uncbuf);
  std::memset(recv_uncbuf, 0, sizeof recv_uncbuf);
  std::memset(previous_recv_uncbuf, 0, sizeof previous_recv_uncbuf);
  std::memset(&simple_sendbuf, 0, sizeof simple_sendbuf);
  std::memset(&simple_recvbuf, 0, sizeof simple_recvbuf);

  sock=listen_socket.accept();

  cout << "socket connection to client established ..." << endl;
 
  int num_txts = 1; // 1 + txt.getSlavesNumber(); ?

  for (int k=0; k<num_txts; k++) {
    for (int i=0; i<4; i++) {
      txt_conf[k].motor[i]        = txt_conf[k].previous_motor[i] = 1; // motor M
      txt_conf[k].out[2*i]        = std::make_shared<ft::Encoder>(txt,i+1);
      std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->startDistance(0);
      std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->startSpeed(0);
      txt_conf[k].out[2*i+1]      = NULL;
    }
    for (int i=0; i<8; i++) {
      txt_conf[k].input_type[i]   = txt_conf[k].previous_input_type[i] = 1; // switch
      txt_conf[k].input_mode[i]   = txt_conf[k].previous_input_mode[i] = 1; // digital
      txt_conf[k].in[i]           = std::make_shared<ft::Switch>(txt, i+1);
    }
    for (int i=0; i<4; i++) {
      txt_conf[k].counter[i]      = std::make_shared<ft::Counter>(txt, i+1);
      txt_conf[k].counter[i]->reset();
    }
  }
  
  txt.update_config();
  m_id = 0;
  previous_m_id = 0;
  connected = true;
  while (connected) {    
      //cout << "waiting ..." << endl;
      auto [size, valid] = sock.recv(recvbuf);
      //cout << std::dec << size << " bytes received: ";
      //for (int k=0; k<size; k++) {
      //  cout << std::hex << (int)recvbuf[k] << " ";
      //}
      //cout << endl;
      previous_m_id = m_id;
      m_id = (uint8_t)recvbuf[0] | (uint8_t)recvbuf[1] << 8 | (uint8_t)recvbuf[2] << 16 | (uint8_t)recvbuf[3] << 24;
      // m_id = (uint32_t)recvbuf[0];
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
          //cout << "  --> sending back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
          sock.send(sendbuf, sendbuf.size());
          i2c_is_online = true;
          std::thread i2c_thread(startI2C);
          i2c_thread.detach();
          break;
        }
        case 0x9BE5082C: { // stop online
          cout << "got: stop online" << endl;
          m_resp_id  = 0xFBF600D2;
          auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
          //cout << "  --> sending back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
          sock.send(sendbuf, sendbuf.size());
          if (previous_m_id != 0xDC21219A) { // query status (this is neccessary for ftScratchTXT)
            i2c_is_online = false;
            camera_is_online = false;
            connected = false;
            sock.close();
            txt.reset();
          }
          break;
        }
        case 0x060EF27E: { // update config
          //cout << "got: update config" << endl;
          m_resp_id  = 0x9689A68C;
          auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
          //cout << "  --> sending back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
          sock.send(sendbuf, sendbuf.size());
          // "<Ihh B B 2s BBBB BB2s BB2s BB2s BB2s BB2s BB2s BB2s BB2s B3s B3s B3s B3s 16h"
          int txt_nr = (int16_t)recvbuf[6];
          txt_conf[txt_nr].config_id = (int16_t)recvbuf[4];
          //cout << "got update_config for txt nr. " << txt_nr << endl;
          if (txt_nr < num_txts) {
            // cout << "Txt[" << txt_nr << "]Configuration ConfigID=" << std::dec << (int)txt_conf[txt_nr].config_id << endl;
            for (int i=0; i<4; i++) {
              txt_conf[txt_nr].motor[i] = (uint8_t)recvbuf[12+i];
              //cout << "Txt[" << txt_nr << "]Configuration Motor[" << i << "]=" << (int)txt_conf[txt_nr].motor[i] << endl; 
              if (txt_conf[txt_nr].motor[i] != txt_conf[txt_nr].previous_motor[i]) {
                if (txt_conf[txt_nr].previous_motor[i] != 0) {
                  //delete txt_conf[txt_nr].out[i*2];
                  txt_conf[txt_nr].out[i*2].reset();
                  txt_conf[txt_nr].out[i*2+1].reset();
                  txt_conf[txt_nr].out[i*2] = std::make_shared<ft::Lamp>(txt, i*2+1 );
                  txt_conf[txt_nr].out[i*2+1] = std::make_shared<ft::Lamp>(txt, i*2+1+1);
                  std::static_pointer_cast<ft::Lamp>(txt_conf[txt_nr].out[i*2])->setBrightness(0);
                  std::static_pointer_cast<ft::Lamp>(txt_conf[txt_nr].out[i*2+1])->setBrightness(0);
                } else {
                  //delete txt_conf[txt_nr].out[i*2];
                  //delete txt_conf[txt_nr].out[i*2+1];
                  txt_conf[txt_nr].out[i*2].reset();
                  txt_conf[txt_nr].out[i*2+1].reset();
                  txt_conf[txt_nr].out[i*2] = std::make_shared<ft::Encoder>(txt, i*2+1);
                  std::static_pointer_cast<ft::Encoder>(txt_conf[txt_nr].out[i*2])->startDistance(0);
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
                //delete txt_conf[txt_nr].in[i];
                if (txt_conf[txt_nr].input_mode[i] != 0) {  // digital input
                  switch (txt_conf[txt_nr].input_type[i]) {
                    case 0:
                      txt_conf[txt_nr].in[i].reset();
                      txt_conf[txt_nr].in[i] = std::make_shared<ft::TrailFollower>(txt, i+1); 
                      break;
                    case 1:
                      txt_conf[txt_nr].in[i].reset();
                      txt_conf[txt_nr].in[i] = std::make_shared<ft::Switch>(txt, i+1); 
                      break;
                    default:
                      cout << "error: unknown digital input type " << txt_conf[txt_nr].input_type[i] << endl;
                      txt_conf[txt_nr].in[i].reset();
                      txt_conf[txt_nr].in[i] = std::make_shared<ft::Switch>(txt, i+1); 
                    break;
                  }
                } else { // analog input
                  switch (txt_conf[txt_nr].input_type[i]) {
                    case 0:
                      txt_conf[txt_nr].in[i].reset();
                      txt_conf[txt_nr].in[i] = std::make_shared<ft::Voltmeter>(txt, i+1);
                      break;
                    case 1:
                      txt_conf[txt_nr].in[i].reset();
                      txt_conf[txt_nr].in[i] = std::make_shared<ft::Resistor>(txt, i+1);
                      break;
                    case 3:
                      txt_conf[txt_nr].in[i].reset();
                      txt_conf[txt_nr].in[i] = std::make_shared<ft::Ultrasonic>(txt, i+1);
                      txt.update_config();
                      //cout << "txt_conf[" << txt_nr << "].in[" << i << "] = Ultrasonic" << endl;
                      break;
                    default:
                      cout << "error: unknown digital input type " << txt_conf[txt_nr].input_type[i] << endl;
                      txt_conf[txt_nr].in[i].reset();
                      txt_conf[txt_nr].in[i] = std::make_shared<ft::Voltmeter>(txt, i+1); 
                    break;
                  }
                }
                txt_conf[txt_nr].previous_input_type[i] = txt_conf[txt_nr].input_type[i];
                txt_conf[txt_nr].previous_input_mode[i] = txt_conf[txt_nr].input_mode[i];
              }
            }
            txt.update_config();
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
          for (int i=0; i<8; i++) {
            simple_recvbuf.txt.pwm[i] = (uint8_t)recvbuf[2*i+4] | (uint8_t)recvbuf[2*i+5]<<8;
            if (simple_recvbuf.txt.pwm[i] != previous_simple_recvbuf.txt.pwm[i]) {
              previous_simple_recvbuf.txt.pwm[i] = simple_recvbuf.txt.pwm[i];
              if (txt_conf[0].motor[i/2] == 1) {
                std::static_pointer_cast<ft::Encoder>(txt_conf[0].out[2*(i/2)])->
                            startSpeed(simple_recvbuf.txt.pwm[2*(i/2)]
                                     -(simple_recvbuf.txt.pwm[2*(i/2)+1]));     // ft::Encoder(txt,i+1)
              } else {
                std::static_pointer_cast<ft::Lamp>(txt_conf[0].out[i])->setBrightness(simple_recvbuf.txt.pwm[i]);
              }
            }
          }
          // encoder
          for (int i=0; i<4; i++) {
            simple_recvbuf.txt.motor_sync[i] = (uint8_t)recvbuf[2*i+20] | (uint8_t)recvbuf[2*i+21]<<8;
            simple_recvbuf.txt.motor_dist[i] = (uint8_t)recvbuf[2*i+28] | (uint8_t)recvbuf[2*i+29]<<8;
            simple_recvbuf.txt.motor_cmd_id[i] = (uint8_t)recvbuf[2*i+36] | (uint8_t)recvbuf[2*i+37]<<8;
            if (simple_recvbuf.txt.motor_cmd_id[i] > previous_simple_recvbuf.txt.motor_cmd_id[i]) {
              //cout << "motor_cmd_id increased in simple_recv" << endl;
              previous_simple_recvbuf.txt.motor_cmd_id[i] = simple_recvbuf.txt.motor_cmd_id[i];
              txt_conf[0].running_motor_cmd_id[i] = simple_recvbuf.txt.motor_cmd_id[i];
              std::static_pointer_cast<ft::Counter>(txt_conf[0].counter[i])->reset();
              if (simple_recvbuf.txt.motor_sync[i] == 0) {
                std::static_pointer_cast<ft::Encoder>(txt_conf[0].out[2*i])->
                            startDistance(simple_recvbuf.txt.motor_dist[i], 0);
              } else {
                std::static_pointer_cast<ft::Encoder>(txt_conf[0].out[2*i])->
                            startDistance(simple_recvbuf.txt.motor_dist[i], 2^i+2^(simple_recvbuf.txt.motor_sync[i]-1));
              }
              txt_conf[0].is_running[i] = std::static_pointer_cast<ft::Encoder>(txt_conf[0].out[2*i])->isRunning();
            }
          }
          // counter
          for (int i=0; i<4; i++) {
            simple_recvbuf.txt.counter_cmd_id[i] = (uint8_t)recvbuf[2*i+44] | (uint8_t)recvbuf[2*i+45]<<8;
            if (simple_recvbuf.txt.counter_cmd_id[i] > previous_simple_recvbuf.txt.counter_cmd_id[i]) {
              previous_simple_recvbuf.txt.counter_cmd_id[i] = simple_recvbuf.txt.counter_cmd_id[i];
              std::static_pointer_cast<ft::Counter>(txt_conf[0].counter[i])->reset();
              txt_conf[0].counter_cmd_id[i] = simple_recvbuf.txt.counter_cmd_id[i];
            }
          }
          break;
        }        
        case  0xFBC56F98: {
          //cout << "got: exchange data compressed" << endl;
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
          // prepare send buffer
          /////////////////////////////

          for (int k=0; k<num_txts; k++) {
            for (int i=0; i<8; i++) {
              //cout << "txt_conf ptr = " << txt_conf[0].in[i]->getConn() << endl;
              //uncbuf[k].txt.input[i] = txt_conf[k].in[i]->getState();

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
          if (TransferDataChanged) {
            send_compbuf.Reset();
            for (int k=0; k<num_txts; k++) {
              // cout << "sizeof(TxtSendDataCompressed) = " << std::dec << sizeof(TxtSendDataCompressed) << endl;
              for (int i=0; i<sizeof(TxtSendDataCompressed)/2; i++) {
                if (uncbuf[k].raw[i] == previous_uncbuf[k].raw[i]) {
                  send_compbuf.AddWord(0, uncbuf[k].raw[i]);
                } else {
                    if (uncbuf[k].raw[i] == 0) {
                      send_compbuf.AddWord(1, 0);
                    } else {
                      send_compbuf.AddWord(uncbuf[k].raw[i], uncbuf[k].raw[i]);
                    }
                  }  
              }
            }
            send_compbuf.Finish();
            memcpy(previous_uncbuf, uncbuf, sizeof(TxtSendDataCompressedBuf)*num_txts);
            crc = send_compbuf.GetCrc();
            previous_crc = crc;
            send_body = send_compbuf.GetBuffer();
            m_extrasize = send_compbuf.GetCompressedSize();
            TransferDataChanged = false;
            //cout << "m_extrasize=" << std::dec << m_extrasize << "  CRC=0x" << std::hex << crc << endl;
            //cout << "send_body: "; for (int i=0; i<m_extrasize; i++) cout << std::hex << (int)send_body[i] << " "; cout << endl;
          } else {
            crc = previous_crc;
            send_body =  send_compbuf.cmpbuf0;
            m_extrasize = 2;
          }

          auto header = pystruct::pack(PY_STRING("<IIIHH"),
            m_resp_id, 
            m_extrasize,              // ,2 extra buffer size
            crc,                      // 0x0bbf0714, crc32 of extra buffer
            0,                        // number of active extensions
            0);                       // dummy align

          std::array<char, 1024> sendbuf;
          for (int i=0; i<header.size(); i++) sendbuf[i]=header[i];
          for (int i=0; i<m_extrasize; i++) sendbuf[header.size()+i] = send_body[i];

          //cout << "  --> sending back " << std::dec << header.size()+m_extrasize << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
          sock.send(sendbuf, header.size()+m_extrasize);

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
                //cout << std::hex << recv_uncbuf[k].raw[i] << "(" << w << ") " << std::flush; 
              } 
            }  
            cout << endl;
            if (recv_crc != recv_compbuf.GetCrc()) {
              //cout << "ERROR: received CRC does not match calculated CRC!" << endl;
              //cout << "transmitted crc = " << recv_crc <<"  calculated crc = " << recv_compbuf.GetCrc() << endl;
            }
            for (int k=0; k<num_txts; k++) {

              // pwm
              for (int i=0; i<8; i++) {
                if (recv_uncbuf[k].txt.pwm[i] != previous_recv_uncbuf[k].txt.pwm[i]) {
                  if (txt_conf[k].motor[i/2] == 1) {
                    std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*(i/2)])->
                                startSpeed(recv_uncbuf[k].txt.pwm[2*(i/2)]
                                         -(recv_uncbuf[k].txt.pwm[2*(i/2)+1]));     // ft::Encoder(txt,i+1)
                  } else {
                    std::static_pointer_cast<ft::Lamp>(txt_conf[k].out[i])->setBrightness(recv_uncbuf[k].txt.pwm[i]);
                  }
                }
              }

              // encoder distance
              for (int i=0; i<4; i++) {
                if (recv_uncbuf[k].txt.motor_cmd_id[i] > previous_recv_uncbuf[k].txt.motor_cmd_id[i]) {
                  txt_conf[k].running_motor_cmd_id[i] = recv_uncbuf[k].txt.motor_cmd_id[i];
                  std::static_pointer_cast<ft::Counter>(txt_conf[k].counter[i])->reset();
                  uncbuf[k].txt.counter_cmd_id[i] = recv_uncbuf[k].txt.counter_cmd_id[i];
                  if (recv_uncbuf[k].txt.motor_sync[i] == 0) {
                    std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->
                                startDistance(recv_uncbuf[k].txt.motor_dist[i], 0);
                  } else {
                    std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->
                                startDistance(recv_uncbuf[k].txt.motor_dist[i], 2^i+2^(recv_uncbuf[k].txt.motor_sync[i]-1));
                  }
                  txt_conf[k].is_running[i] = std::static_pointer_cast<ft::Encoder>(txt_conf[k].out[2*i])->isRunning();
                }
              }

              // counter
              for (int i=0; i<4; i++) {
                if (recv_uncbuf[k].txt.counter_cmd_id[i] > previous_recv_uncbuf[k].txt.counter_cmd_id[i]) {
                  std::static_pointer_cast<ft::Counter>(txt_conf[k].counter[i])->reset();
                  uncbuf[k].txt.counter_cmd_id[i] = recv_uncbuf[k].txt.counter_cmd_id[i];
                  txt_conf[k].counter_cmd_id[i] = uncbuf[k].txt.counter_cmd_id[i];
                }
              }
            }

          }
          break;
        }
        case 0x882A40A6: { // startCameraOnline
          cout << "got: startCameraOnline" << endl;
          m_resp_id  = 0xCF41B24E;
          auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
          cout << "  --> sending back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
          sock.send(sendbuf, sendbuf.size());
          auto [dummy_m_id, width, height, framerate, powerlinefreq] = pystruct::unpack(PY_STRING("<I4i"), recvbuf);
          if (!camera_is_online) {
            camera_is_online = true;
            std::thread camthread(camThread, width, height, framerate);
            camthread.detach();
          }
          break;
        }       
        case 0x17C31F2F: { // stopCameraOnline
          cout << "got: stopCameraOnline" << endl;
          m_resp_id  = 0x4B3C1EB6;
          auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
          cout << "  --> sending back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
          sock.send(sendbuf, sendbuf.size());
          camera_is_online = false;
          break;
        }   
        case 0xACAB31F7: { // "Stop all running programs" pressed in ROBOPro
          cout << "got: ACAB31F7 (stop all running programs pressed in ROBOPro)" << endl;
          m_resp_id  = 0x0; // 0x15B1758E;
          auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
          cout << "  --> sending back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
          sock.send(sendbuf, sendbuf.size());
          connected = false;
          camera_is_online = false;
          i2c_is_online = false;
          sock.close();
          txt.reset();
          break;
        }
        case 0xDAF84364: { // Upload Program to TXT
          cout << "got: DAF84364 (Upload Program to TXT)" << endl;
          m_resp_id  = 0x5170C53B; // +sendbuf (Speicherlayout) = 00 60 7d b6 00 00 40 00 48 d9 6b b6 00 24 00 00
          // ("kein Speicherlayout erhalten!"-Fehler in ROBOPro, wenn sendbuf leer)
          auto sendbuf = pystruct::pack(PY_STRING("<I"), m_resp_id);
          cout << "  --> sending back " << std::dec << sendbuf.size() << " bytes, m_resp_id=0x" << std::hex << m_resp_id << endl << endl;
          sock.send(sendbuf, sendbuf.size());
          connected = false;
          sock.close();
          txt.reset();
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
          sock.close();
          txt.reset();
          break;
        }
      } 
      sleep_for(5ms);
  } // while(connected)
  
  } // while(true)

}
