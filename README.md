# ESP32-S31 有线转 Wi-Fi 路由器

适用于乐鑫 ESP32-S31-Function-CoreBoard-1：板载 RJ45 / YT8531 RGMII PHY，Wi-Fi 使用 AP 模式。

有线网络通过 DHCP 为板子分配 WAN 地址、网关和 DNS；手机、电脑接入热点后，由板子提供 DHCP，通过 IPv4 NAPT 访问上游网络。IPv6 通过以太网与热点之间的二层透传，直接使用上游路由器提供的地址和默认路由。

## 构建与使用

使用支持 esp32s31 预览目标的 ESP-IDF 6.1 环境。先打开 ESP-IDF PowerShell/终端，然后执行：

    cd D:\esp32-router
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
- 修改 SSID、Wi-Fi 密码、AP 网关、子网掩码和 DNS；密码留空表示保留原值。
- LAN 必须是 RFC1918 私有 IPv4 网段，掩码连续，范围 /8–/28，网关不能是网络地址或广播地址，且须留足 DHCP 地址。
- DNS 留空时使用上游 DNS，上游未提供或未连接时使用备用 DNS；手动填写的 DNS 不会被上游覆盖。

点击“保存并重启”会先校验并提交整份配置到 NVS，成功后约 2 秒重启。按页面提示重新连接热点并打开新网关。掉电再上电后会读取已保存配置；普通重新烧录不会清除 NVS，`erase-flash` 会清除配置。若保存的网段与 WAN 冲突，使用临时网段，不覆盖已保存的首选网段。

管理服务仅绑定 AP 接口，使用 HTTP、HttpOnly/SameSite 会话 Cookie、30 分钟会话及登录频率限制；同一时间保留一个登录会话。密码不会通过状态接口返回。此版本未启用 HTTPS、Flash/NVS 加密，请在可信 Wi-Fi 内使用并修改默认密码。DHCP 尚未分配地址的设备会显示 IP 暂不可用；静态地址只能在设备通信后通过 ARP 尽力识别。

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
- 当前提供 IPv4 NAT、IPv6 透传和管理网页；未实现 OTA。NVS 初始化失败会报错，不会自动擦除已有 NVS。

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
