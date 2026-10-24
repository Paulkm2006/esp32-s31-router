# ESP32-S31 有线转 Wi-Fi 路由器

适用于乐鑫 ESP32-S31-Function-CoreBoard-1：板载 RJ45 / YT8531 RGMII PHY，Wi-Fi 使用 AP 模式。

有线网络通过 DHCP 为板子分配 WAN 地址、网关和 DNS；手机、电脑接入热点后，由板子提供 DHCP，通过 IPv4 NAPT 访问上游网络。IPv6 通过以太网与热点之间的二层透传，直接使用上游路由器提供的地址和默认路由。

## 构建与使用

使用支持 esp32s31 预览目标的 ESP-IDF 6.1 环境。先打开 ESP-IDF PowerShell/终端，然后执行：

    cd D:\esp32-router
    git submodule update --init --recursive
    idf.py --preview set-target esp32s31
    idf.py menuconfig
    idf.py build
    idf.py -p COMx flash monitor

将 COMx 替换为实际串口。退出串口监视器用 Ctrl+]。

Bootloader 保持 IDF 默认的 Size 优化，避免超出当前引导分区。复用旧 sdkconfig 时，将 Component config → cJSON 的最大嵌套深度设为 8 或以下，限制登录请求的解析栈用量；并启用 `CONFIG_ETH_TRANSMIT_MUTEX=y`，保护 IPv4 与 IPv6 的并发以太网发送。全新配置已通过 sdkconfig.defaults 设置。

在 menuconfig 的 Router Wi-Fi AP 中设置 SSID、密码、信道和最大接入数。默认值：

| 配置 | 默认值 |
| --- | --- |
| SSID | ESP32-S31-Router |
| WPA2 密码 | ChangeMe-S31! |
| 2.4 GHz 信道 | 6 |
| 最大接入数 | 4 |
| LAN 网关 | 192.168.4.1/24 |
| 备用 DNS | 1.1.1.1 |
| 初始管理密码 | ChangeAdmin-S31! |

使用前修改默认密码。SSID 限 1–32 字节，密码限 8–63 字节。menuconfig 设置首次启动的默认值；网页保存的配置优先，从 NVS 读取，固件不输出密码日志。

将 RJ45 接到有 DHCP 服务且能访问互联网的上游 LAN 口。板子启动后连接热点即可，无需为客户端设置静态 IP。热点在未插网线时也会运行，但此时没有互联网转发。

日志出现以下内容表示有线 DHCP 和 NAPT 已准备好：

    router: WAN IP: ... mask: ... gateway: ...
    router_ap: AP gateway: ..., DNS: ...
    router: Internet forwarding ready (Ethernet -> Wi-Fi AP)

客户端成功取得地址后还会出现：

    router: LAN DHCP lease: ...

这些日志只表明板上接口已配置；客户端能否真正访问互联网仍需按下面的步骤验收。

## 管理页面

连接热点后，在浏览器打开当前 AP 网关，例如 http://192.168.4.1/。页面内置于固件，无需互联网；没有接入网线时也能登录和配置。WAN 网段冲突可能触发临时网关，请以客户端 DHCP 获取的网关为准。

初始管理密码为 `ChangeAdmin-S31!`，与 Wi-Fi 密码独立；可在 menuconfig 的 Router Wi-Fi AP 中修改初始值，或登录页面后设置新管理密码。

- 查看当前连接设备的 IP、MAC 和本次连接时长，每 5 秒更新；断开重连后重新计时。
- 查看有线端口链路、IP、掩码、网关、DNS、MAC、协商速率与双工模式。
- 展开“系统日志”查看应用日志，每 5 秒增量更新；串口输出继续保留。
- 修改 SSID、Wi-Fi 密码、AP 网关、子网掩码和 DNS；密码留空表示保留原值。
- LAN 必须是 RFC1918 私有 IPv4 网段，掩码连续，范围 /8–/28，网关不能是网络地址或广播地址，且须留足 DHCP 地址。
- DNS 留空时使用上游 DNS，上游未提供或未连接时使用备用 DNS；手动填写的 DNS 不会被上游覆盖。

点击“保存并重启”会先校验并提交整份配置到 NVS，成功后约 2 秒重启。按页面提示重新连接热点并打开新网关。掉电再上电后会读取已保存配置；普通重新烧录不会清除 NVS，`erase-flash` 会清除配置。若保存的网段与 WAN 冲突，使用临时网段，不覆盖已保存的首选网段。

管理服务仅绑定 AP 接口，使用 HTTP、HttpOnly/SameSite 会话 Cookie、30 分钟会话及登录频率限制；同一时间保留一个登录会话。密码不会通过状态接口返回。此版本未启用 HTTPS、Flash/NVS 加密，请在可信 Wi-Fi 内使用并修改默认密码。DHCP 尚未分配地址的设备会显示 IP 暂不可用；静态地址只能在设备通信后通过 ARP 尽力识别。

日志在 `app_main` 开始时接入 ESP-IDF 日志回调，使用 2 MiB PSRAM 环形缓存，
保留最近 24 小时、最多 2048 条记录；容量满时覆盖最旧记录，重启或断电后清空。
每条最多 1015 字节，超长内容以 `[truncated]` 标记；日志中的时间是设备启动后的时间。
启动早期、Bootloader、崩溃输出及直接 `printf` / ROM 输出不经过此回调，仍需从串口查看。
日志接口 `/api/logs` 需要管理登录，仅在展开日志面板时轮询；退出登录会清空浏览器中的日志。
若无法分配 PSRAM，路由功能继续运行，日志面板提示缓存不可用。

## 组件管理

本项目已通过以下命令添加依赖，不需要在已有检出中重复执行：

    idf.py add-dependency "espressif/ethernet_init==1.4.1"

main/idf_component.yml 声明依赖，dependencies.lock 固定解析结果。idf.py reconfigure/build 自动下载到 managed_components，不要手工复制或修改该目录。

当前使用 ethernet_init 1.4.1，并通过其 Kconfig 条件依赖拉取 yt8531 0.2.0；JSON 使用 IDF 6.1 推荐的受管组件 espressif/cjson。Wi-Fi、DHCP、DNS、HTTP 服务及 NAPT 使用 IDF 自带组件。更新受管依赖可运行 idf.py update-dependencies，更新后重新构建和上板验收。

## 板级配置

sdkconfig.defaults 针对官方 CoreBoard 的 16 MB Flash 和 YT8531 提供默认值。RGMII 数据管脚沿用受管组件的 S31 默认配置：

| 信号 | GPIO |
| --- | --- |
| MDC / MDIO / PHY RESET | 5 / 6 / 7 |
| TXD0 / TXD1 / TXD2 / TXD3 | 8 / 9 / 10 / 11 |
| TX_CTL / TX_CLK | 12 / 13 |
| RX_CLK / RX_CTL | 14 / 15 |
| RXD0 / RXD1 / RXD2 / RXD3 | 19 / 18 / 17 / 16 |

PHY 使用板载晶振，PHY_REF_CLK 为 -1，PHY 地址自动探测。保留原 IDF 示例的 RX 约 2 ns、TX 13 步时钟延时，使用 APLL 作为 RGMII TX 时钟源。

如果板型或硬件修订不同，请先核对原理图，并在 Component config → Ethernet Driver Configuration 中调整。现有 sdkconfig 优先于 sdkconfig.defaults；已有工程改默认值后，应在 menuconfig 中同步修改。S31 目前仍属于预览支持。

## 转发与恢复行为

- Ethernet 是唯一上游，仅启用 Wi-Fi AP 模式；DHCP 默认路由指向 Ethernet。
- AP 使用保存的私有子网。若与 WAN 重叠，依次尝试 /24 网关 192.168.4.1、10.42.0.1、172.20.0.1，页面显示当前实际网关。
- 检查同时考虑 WAN 和 LAN 的掩码，覆盖上游 /16、/8 等较大子网；所有候选均冲突时停止转发并报错。
- 上游取得 IPv4 地址后，将其主 DNS 或备用 DNS 下发给热点客户端；均未提供时使用配置的备用 DNS。
- 网关或 DNS 改变时，已有 Wi-Fi 客户端会被断开一次，以重新连接并取得正确租约。
- 拔网线或 WAN 丢失 IP 时关闭 NAPT，保留热点和本地 DHCP；重新取得 WAN 地址后恢复。
- 仅 DNS 改变而 IP/网关/掩码均不变的 DHCP 续租不会触发 IDF GOT_IP 事件；此时重新插拔网线或重启板子以同步 DNS。
- NAPT 不提供入站防火墙；能添加到 LAN 子网路由的上游设备仍可主动访问热点客户端。
- 当前提供 IPv4 NAT、IPv6 透传、USB 存储及管理网页；未实现 OTA。NVS 初始化失败会报错，不会自动擦除已有 NVS。

## USB 网络存储

将 U 盘接到板上的 USB Type-A Host 口。默认开启 PSRAM（100 MHz）、UTF-8 长文件名与
4 MiB 应用分区；Flash 中原有 NVS 和 PHY 分区的地址保持不变。已有 sdkconfig 需要在
menuconfig 中同步开启 PSRAM、选择自定义分区表 partitions.csv，并启用 FatFs 的
堆分配长文件名、UTF-8 编码、4096 字节最大扇区及 16 个文件锁。不要使用未跟踪的旧分区文件。

存储支持一个 USB MSC 设备、一个 FAT32 卷，SMB 服务仅在 Wi-Fi AP 接口提供。
它接受 SMB3 协议，要求账号认证与签名，不提供匿名访问或 SMB1/SMB2 降级。
最多两个 SMB 连接，每个连接最多八个文件或目录句柄，读写块最大 32 KiB。
SMB 缓冲与句柄放在 PSRAM 中；未检测到 PSRAM 时存储服务无法启动。

登录管理页面后，在“USB 网络存储”中操作：

1. 查看磁盘容量、FAT32 已用/可用空间、共享地址、服务状态和 PSRAM 占用。
2. 设置共享开关、共享名称、SMB 用户名和密码，再点击“保存共享设置”。首次启用必须设置密码；
   密码留空保留原值。设置独立保存在 NVS，保存会断开 SMB 客户端，不重启路由器。
3. 插盘后自动尝试挂载 FAT32。安全卸载后可点“挂载”恢复使用，也可拔插磁盘。
4. 拔盘前点击“安全卸载”，等待页面显示未挂载。设备会先停止共享、关闭文件并同步磁盘；
   同步失败时拒绝卸载并显示错误。
5. 格式化需输入 `FORMAT`。此操作会停止 SMB、卸载卷、清空整个磁盘的分区表并创建一个 FAT32 分区，
   完成后挂载并按保存的设置恢复共享。确认绑定当前磁盘，换盘后需要重新确认。

挂载失败不会自动格式化；非 FAT32、损坏或未格式化的磁盘会显示错误，等待管理员处理。
格式化运行于后台，期间不要断电或拔盘。通过网络设置页面重启前也会先安全卸载。
FAT32 单文件最大 4 GiB − 1 字节，不支持 POSIX 权限、符号链接或 ACL。
当前未实现 SMB 字节范围锁和覆盖目标文件的原子重命名；已有文件可直接覆盖写入，
替换式重命名会返回冲突。适合一般文件传输，不适合要求这些语义的数据库或虚拟机磁盘。
SMB 密码不会通过状态接口返回；当前管理页面使用 HTTP，NVS 未启用加密，仍应在可信 Wi-Fi 中管理。

Windows：连接热点后，将页面显示的地址（例如 `\\192.168.4.1\usb`）输入文件资源管理器，
或使用“映射网络驱动器”，输入刚设置的 SMB 用户名和密码。更换账号后需要断开原共享连接，
并更新 Windows 保存的凭据。服务不提供共享自动发现，请直接使用页面显示的共享路径。

Linux：安装发行版的 cifs-utils，创建挂载目录后执行，例如：

    sudo mount -t cifs //192.168.4.1/usb /mnt/esp32 -o username=router,vers=3.1.1,sec=ntlmssp,sign,noserverino

请按页面实际网关、共享名称和用户名替换示例值；命令会提示输入密码。
`noserverino` 避免依赖 FAT32 重命名前后不保证稳定的文件编号。
在电脑上卸载共享后，仍需在管理页面执行设备端“安全卸载”再拔 U 盘。

Type-A 口限流 500 mA；高功耗 SSD/移动硬盘应使用独立供电设备。
USB 链路标称 480 Mbps 不代表文件传输速度；吞吐与路由延迟需实板并发测试。

### SMB/FAT32 宿主集成测试

Linux 环境中先准备 Python 虚拟环境并安装 smbprotocol 1.17.0，然后执行：

    cmake -S tests/storage_host -B .cache/smb-host -DIDF_PATH=/path/to/esp-idf
    cmake --build .cache/smb-host -j
    python tests/run_smb_tests.py

测试在端口 1445 启动临时服务，复用生产认证、文件处理和磁盘生命周期代码；仅替代 USB 块设备，
使用临时 64 MiB 磁盘镜像和真实 FatFs。覆盖错误密码、SMB3 签名、SMB2 拒绝、长中文文件名、
读写、零填充、截断、重命名、分页枚举及删除，同时验证挂载失败不擦盘、同步失败不卸载。
它不替代实板 USB 热插拔、Windows 文件资源管理器、Linux 内核 CIFS 挂载或并发路由性能验收。

内存检查可在配置命令中追加
`-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"`，重建后运行：

    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python tests/run_smb_tests.py

libsmb2 以固定版本 Git 子模块保存。components/smb3/patch_sources.cmake 在构建目录生成修复后的源码，
修复自定义响应导致的请求生命周期问题、CREATE 名称泄漏、空密钥派生和未对齐 UTF-16 读取；
宿主和固件使用同一份修复，不修改第三方检出。更新子模块时需要复跑上述内存检查。

## IPv6 透传

适用于上游普通路由器已经提供 IPv6 的场景：电脑直接连接该路由器 LAN 口时应能使用 IPv6，且上游允许同一网口下出现多个设备 MAC。板子只透传未带 VLAN 标签的 IPv6 以太网帧，IPv4 仍使用原来的 DHCP/NAPT。

上游的 RA、邻居发现、DHCPv6 和 IPv6 数据包直接在 Ethernet 与 Wi-Fi AP 间传送，不改写地址或 Hop Limit。客户端的 IPv6 地址和默认路由由上游提供；无需开启 lwIP 的 IPv6 forwarding 或 SLAAC，也无需设置新的 LAN IPv6 前缀。透传依赖以太网链路和 AP 启动状态，不依赖板子的 IPv4 DHCP 是否成功。

启动时应出现以下日志，表示透传模块已安装；它不代表客户端已经获得 IPv6 或通过互联网连通性验证：

    router_ipv6: IPv6 passthrough enabled (Ethernet <-> Wi-Fi AP)

IPv6 客户端与上游处于同一二层网络，入站访问控制依赖上游路由器的 IPv6 防火墙。此实现不提供 IPv6 防火墙、NAT66、独立下游前缀或前缀委派，也不为板子本身配置 IPv6 地址。管理页面及设备列表仍使用 IPv4。

上板后，在连接热点的电脑上验收（关闭其他网络出口；Windows 使用 `curl.exe`）：

1. 查看客户端地址与路由，确认具有上游提供的 IPv6 地址和默认路由；只有 `fe80::` 链路本地地址不算成功。
2. 使用 `ping -6 2606:4700:4700::1111` 检查 IPv6 IP 连通性，再用 `curl -6 --connect-timeout 10 https://example.com/` 验证 DNS、TCP 和 HTTPS。
3. 同时检查原有 IPv4 上网和管理页面，确认双栈均可使用。
4. 拔插网线、重启上游和重连热点后，确认客户端能重新发现 IPv6 路由并恢复上网。

若没有获得 IPv6，先检查上游 RA 是否启用；透传不能让 IPv4-only 上游产生 IPv6。宿主测试覆盖帧转发、MAC 过滤、断线恢复、错误处理和缓冲区释放，实际多播收发、吞吐与客户端兼容性仍需实板验证。

## 验证

在有 C 编译器的机器上运行子网判断测试：

    cc -std=c11 -Wall -Wextra -Werror tests/test_subnet.c -o build/test_subnet
    ./build/test_subnet

Windows 可使用同等的宿主 C 编译器。ESP-IDF 的 RISC-V 交叉编译器生成的程序不能直接在 Windows 运行。

管理页面脚本行为测试（Node.js，无第三方依赖）：

    node tests/test_web_ui.mjs

配置校验、NVS 提交/重载/失败恢复、登录会话、子网及 IPv6 透传宿主测试：

    python tests/run_host_tests.py

该测试使用宿主 C 编译器及 IDF 头文件，NVS 为内存替身，不代表真实 Flash 掉电验证。

有实板和 pytest-embedded 环境时，可运行串口 / WAN ping 冒烟测试：

    pytest pytest_router.py --embedded-services esp,idf --target esp32s31 --port COMx

完整上网验收需要一台连接热点的手机或电脑：

1. 确认客户端通过 DHCP 获取当前 AP 子网地址、网关和上游 DNS。
2. 暂时禁用客户端其他互联网出口（例如手机移动数据），避免误判。
3. ping 当前 AP 网关，再访问外部 IP，随后打开 HTTPS 网站验证 DNS 和 TCP 转发。
4. 拔掉网线，确认热点仍可接入；重新插入后确认上网恢复。
5. 将上游设为与 192.168.4.0/24 重叠的网络，确认日志显示新的 AP 网段，客户端重新获取地址后仍可上网。
6. 不插网线冷启动，确认客户端能通过 DHCP 获取地址并打开管理页面；未登录时状态和配置接口应返回 401。
7. 登录后确认设备的 IP/MAC、时长及拔插网线后的端口状态；设备断开重连后时长重新计数。
8. 修改 SSID、两种密码、LAN 网关/掩码及手动 DNS，保存后按提示重连；断电再上电，检查配置及客户端 DHCP 租约仍正确。
9. 将 DNS 留空后确认重新跟随上游；输入无效密码或非私有/不连续掩码时应拒绝保存，原配置继续有效。

项目构建与软件测试不能证明 PHY 时序、实测吞吐量或以上完整上网流程。首次烧录前请确认板子 Flash 容量和串口；flash 会替换现有应用固件。

## 官方参考

- [CoreBoard 原理图](https://dl.espressif.com/schematics/esp32-s31-function-coreboard-1-schematics.pdf)
- [S31 支持状态](https://developer.espressif.com/hardware/esp32s31/)
- [ethernet_init 组件](https://components.espressif.com/components/espressif/ethernet_init/versions/1.4.1/readme)
- [ESP-NETIF API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/network/esp_netif_programming.html)
- [ESP-IDF Ethernet / SoftAP 二层转发示例](https://github.com/espressif/esp-idf/tree/v6.1/examples/network/eth2ap)
- [IDF 6.x JSON 组件迁移说明](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/migration-guides/release-6.x/6.0/protocols.html)
