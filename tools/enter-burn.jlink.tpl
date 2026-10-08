// 草案（未进仓库）：让目标机在下次复位后停在 bootloader 的烧写模式。
// 用法：把 @ADDR@ 换成 bootloader 里 _snoinit 的实际地址（从 ELF 读符号，别写死），
//       JLink.exe -device STM32F103ZE -if SWD -speed 4000 -autoconnect 1
//                 -ExitOnError 1 -NoGui 1 -CommandFile <这个文件>
// 各步原因：
//   r      NRST 复位（走 J-Link，不碰 CH340 的 DTR 支路）
//   h      halt —— 写内存前必须让 CPU 停住
//   w4     写 32 位标志。V6.86 文档写法是 "w4 <addr>, <data>"，首次执行核对一下
//   mem    读回确认
//   r / g  再复位并放跑 —— bootloader 一开头就读这个标志
r
h
w4 @ADDR@, 0xB00710AD
mem @ADDR@, 1
r
g
qc
