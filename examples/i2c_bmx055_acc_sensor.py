import ftrobopy
import time
import struct

txt=ftrobopy.ftrobopy('192.168.7.2', 65000)

def calc_acc(msb, lsb):
  bits=12 # number of bits for two's complement
  res = (msb * 256 + lsb) >> 4
  if (res & (1 << (bits-1))):
    mask = (1 << bits) - 1
    res = -(((res ^ mask) & mask) + 1)
  return res / 0.512

txt.i2c_write(0x18, 0x14, 0xB6)  # softreset
res=txt.i2c_read(0x18, 0x00)
if res[0] == 0xfa:
  print("Found BMX055 Acceleration Sensor at address 0x18")
  txt.i2c_write(0x18, 0x11, 0x00)  # set normal power mode
  txt.i2c_write(0x18, 0x3e, 0x00)  # set fifo stream mode
  txt.i2c_write(0x18, 0x0f, 0x05)  # set g-range to +-4g
  txt.i2c_write(0x18, 0x10, 0x0a)  # set filter to 31.25 Hz
                                                        
  for i in range(10000):                                   
    chip_temperature=(txt.i2c_read(0x18, 0x08)[0]) / 2 + 23
    ambient_temperature=chip_temperature - 5
    print("chip temperature: ", chip_temperature, ", ambient temperature: ", ambient_temperature)
    x_lsb=txt.i2c_read(0x18, 0x02, data_len=1, debug=False)[0]
    x_msb=txt.i2c_read(0x18, 0x03, data_len=1, debug=False)[0]
    y_lsb=txt.i2c_read(0x18, 0x04, data_len=1, debug=False)[0]
    y_msb=txt.i2c_read(0x18, 0x05, data_len=1, debug=False)[0]
    z_lsb=txt.i2c_read(0x18, 0x06, data_len=1, debug=False)[0]
    z_msb=txt.i2c_read(0x18, 0x07, data_len=1, debug=False)[0]
    print("{xg:5.1f} {yg:5.1f} {zg:5.1f}".format(xg=calc_acc(x_msb, x_lsb), yg=calc_acc(y_msb, y_lsb), zg=calc_acc(z_msb, z_lsb)))
    time.sleep(0.1)                                     
else:
  print("no bmx055 acceleration sensor found at address 0x18")
  