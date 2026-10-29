// 资产上传模板：设置升级标志，下次复位直接进入资产传输阶段。
// 有效 no-init Fault 仍优先停留；本模板不清除异常现场。
// 用法：把 @ADDR@ 换成 bootloader 里 _snoinit 的实际地址（从 ELF 读符号，别写死），
//       JLink.exe -device STM32F103ZE -if SWD -speed 4000 -autoconnect 1
//                 -ExitOnError 1 -NoGui 1 -CommandFile <这个文件>
// 各步原因：
//   r      请求目标复位（不碰 CH340 的 DTR/RTS 支路）
//   h      halt —— 写内存前必须让 CPU 停住
//   w4     写 32 位标志。V6.86 文档写法是 "w4 <addr>, <data>"，首次执行核对一下
//   mem    读回确认
//   r / g  再复位并放跑 —— 顶层 HSM 处理异常后检查升级标志
r
h
w4 @ADDR@, 0xB00710AD
mem @ADDR@, 1
r
g
qc
