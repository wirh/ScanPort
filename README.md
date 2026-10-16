# ScanPort

ScanPort is a lightweight 32-bit Windows GUI TCP port scanner written in C.

## Features

- Scan an IPv4 address range and one or more TCP ports.
- Configure connection timeout and worker count.
- Stop an active scan and keep the results collected so far.
- Choose whether to clear previous results before starting another scan.
- Display up to 10,000 open-port entries while continuing to count all findings.

Only scan systems and networks that you own or are authorized to test.

## Build on Windows

Install [MSYS2](https://www.msys2.org/) and the 32-bit MinGW-w64 toolchain:

```sh
pacman -Syu
pacman -S --needed mingw-w64-i686-toolchain
```

Run `build.bat` for a release executable, or `build.bat debug` for a build with
debug symbols. The default MSYS2 installation under `C:\msys64` is detected
automatically.

To debug in Visual Studio Code, install the Microsoft C/C++ extension, open this
folder, set breakpoints in `main.c`, and press F5.

## Build on Linux

On Ubuntu, install the cross-compiler and run the supplied script:

```sh
sudo apt install gcc-mingw-w64-i686
bash build.sh
```
<img width="482" height="307" alt="image" src="https://github.com/user-attachments/assets/e25fa34a-ac27-44c6-94a5-d3e3facb6072" />
<img width="482" height="307" alt="image" src="https://github.com/user-attachments/assets/3decda9b-fff3-4a46-8fd1-9672d78217ad" />



## Author

FuYun — [fuyun@mosx.cn](mailto:fuyun@mosx.cn)

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
