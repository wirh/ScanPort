ScanPort 源码说明
作者：FuYun（fuyun@mosx.cn）
许可证：MIT，详见 LICENSE

文件：
main.c 源码（UTF-8 编码，含中文界面文字）
scanport.ico 程序图标（256/48/32/16 四个尺寸）
scanport.rc 资源文件（把图标嵌进 exe）
build.bat Windows 编译脚本（双击运行）
build.sh Linux 编译脚本（交叉编译）


任意文本编辑器，源码为 UTF-8 编码。
推荐：VS Code、Notepad++、Sublime Text。
注意：不要用旧版 Windows 记事本（默认存为 GBK，会导致中文乱码或编译出错）。
如果编辑器把文件存成了 GBK，编译时把 build.bat 里的
-finput-charset=UTF-8 改成 -finput-charset=GBK。


MinGW-w64，32 位目标（i686），因为程序是 32 位 PE32 GUI。
安装方式（二选一）：
1) MSYS2（推荐）：安装 MSYS2 后，在“MSYS2 MinGW 32-bit”终端里执行
pacman -Syu
如果更新关闭了终端，请重新打开后再次运行，直到更新完成；然后执行
pacman -S --needed mingw-w64-i686-toolchain
默认安装到 C:\msys64 时，无需手动配置 PATH。
2) 独立 MinGW-w64：从 https://www.mingw-w64.org/downloads/ 下载 i686 版本，
解压后把 bin 目录加入系统 PATH。
Linux（Ubuntu）：sudo apt install gcc-mingw-w64-i686，然后运行 build.sh。

（build.bat 里已写好，双击即可）
i686-w64-mingw32-gcc -Os -s -mwindows -nostdlib... -o ScanPort.exe main.c -lkernel32 -luser32 -lgdi32 -lws2_32 -lcomctl32

VS Code（Windows）：
1. 安装 VS Code 扩展 Microsoft C/C++（ms-vscode.cpptools）。
2. 打开本项目文件夹，在 main.c 设置断点后按 F5 编译 Debug 版本并启动调试；
   启动时会先暂停，继续运行后可在断点处调试。按 Ctrl+Shift+B 编译 Debug 版本。
3. 通过命令面板运行 “Tasks: Run Task” → “ScanPort: build (release)”
   编译发布版本。

build.bat 会自动检测 C:\msys64\mingw32\bin 中的 32 位工具链；若安装在其他
位置，请将其 mingw32\bin 目录加入 PATH，并将 .vscode\launch.json 与
.vscode\c_cpp_properties.json 中的工具路径改为实际安装位置。也可在命令行
运行 build.bat debug 编译带调试符号的版本，直接运行 build.bat 则编译优化后的发布版本。

- 生成的 ScanPort.exe 是 32 位 GUI 程序，无 CRT（-nostdlib）；文件大小取决于
  编译器和图标资源，本机 Release 构建约 45KB。
- Entry 是真正的入口，取 WinMain 参数再调用（不要直接以 WinMain 为入口，
否则 WinMain 读到栈上垃圾参数，建窗失败、双击无反应）。
- 中文界面文字在源码里是 UTF-8，编译时由 -fexec-charset=GBK 转成 GBK。
- 功能：起始 IP 填入后，结束 IP 自动填充前 3 段，第 4 段默认为 254。
- 扫描时底部进度条实时显示进度；IP 没填完点扫描会提示“IP填写不完整”。
- IP 地址数量及总扫描任务数不再设人为上限（任务计数支持 64 位）；扫描整个 IPv4
  地址空间和大量端口可能需要很长时间。为避免结果列表占用过多内存，界面最多显示
  10000 条开放结果，扫描仍会继续并统计全部结果。
- 扫描过程中可点击“终止扫描”请求停止；工作线程每 100 毫秒检查一次停止请求，
  完成后显示已检查任务数和已发现的开放端口数。
- 再次扫描且结果列表非空时，会询问是否清除上次结果；选择“否”会保留旧结果并
  追加新扫描结果。
