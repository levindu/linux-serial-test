# 串口测试说明

本串口测试的原理是，发送方发送连续变化的字节流，接收方检测收到的字节流是否也是连续变化，如果当前字节不是连续变化，则错误计数加一，并以此字节作为新的顺序基数继续比较。

## 软件安装

将 linux-serial-test.tgz 包分别拷贝到 PC 和开发板上，并解压即可。

    tar zxf linux-serial-test.tgz

## RS485 测试

现在开发板上的 RS485 一般都是两线的，只能半双工传输，则同一时刻仅能发送或接收，不能同时发接和接收。

以下假设：
 - PC 上的 RS485 串口设备是 /dev/ttyUSB1, 波特率为 9600。
 - 开发板上的 RS485 串口设备是 /dev/ttyS2, 波特率为 9600。

### 开发板发送，PC 接收

PC:
  ./rs485.sh recv /dev/ttyUSB1 9600 

开发板：
  ./rs485.sh send /dev/ttyS2 9600 

### 开发板接收，PC 发送

开发板：
  ./rs485.sh recv /dev/ttyS2 9600 

PC:
  ./rs485.sh send /dev/ttyUSB1 9600 

## RS232/UART 测试

RS232/UART 能全双工传输，即可同时发接和接收。

以下假设：
 - PC 上的串口设备是 /dev/ttyUSB1, 波特率为 115200。
 - 开发板上的串口设备是 /dev/ttyS2, 波特率为 115200。

PC:
  ./rs232.sh /dev/ttyUSB1 115200

开发板：
  ./rs232.sh /dev/ttyS2 115200
