import numpy as np
import serial

ser = serial.Serial(port='COM8', baudrate=1000000, bytesize=8, stopbits=2, timeout=0.05)

# send command
ser.write(bytes([1, 2, 3, 4]))

# receive
result = ser.read(48)

t = []
i = 0
arr_timestamp = np.frombuffer(result[48*i:48*i+32], dtype='>u8').astype(int)
arr_tdc = np.frombuffer(result[48*i+32:48*i+48], dtype='>u4').astype(int)
t.append(arr_timestamp.tolist() + (arr_tdc >> 8).tolist() + (arr_tdc&0xff).tolist())
# contains: timestamp_1, _2, _3, _4; tdc_coarse_time_1, _2, _3, _4; tdc_fine_time_1, _2, _3, _4
# tdc fine time needs calibration

t = np.array(t)
print(t.shape)
print(t)
