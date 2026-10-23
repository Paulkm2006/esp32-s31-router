# ESP32-S31 有线转 Wi-Fi 路由器

适用于乐鑫 ESP32-S31-Function-CoreBoard-1：板载 RJ45 / YT8531 RGMII PHY，Wi-Fi 使用 AP 模式。

有线网络通过 DHCP 为板子分配 WAN 地址、网关和 DNS；手机、电脑接入热点后，由板子提供 DHCP，通过 IPv4 NAPT 访问上游网络。

## 构建与使用

使用支持 esp32s31 预览目标的 ESP-IDF 6.1 环境。先打开 ESP-IDF PowerShell/终端，然后执行：

    cd D:\esp32-router
    idf.py --preview set-target esp32s31
    idf.py menuconfig
    idf.py build
    idf.py -p COMx flash monitor

将 COMx 替换为实际串口。退出串口监视器用 Ctrl+]。

在 menuconfig 的 Router Wi-Fi AP 中设置 SSID、密码、信道和最大接入数。默认值：

| 配置 | 默认值 |
| --- | --- |
| SSID | ESP32-S31-Router |
| WPA2 密码 | ChangeMe-S31! |
| 2.4 GHz 信道 | 6 |
| 最大接入数 | 4 |
| LAN 网关 | 192.168.4.1/24 |
| 备用 DNS | 1.1.1.1 |

使用前修改默认密码。SSID 限 1–32 字节，密码限 8–63 字节；超出范围会在编译时报错。Wi-Fi 设置保存在本地 sdkconfig，固件不输出密码日志。

将 RJ45 接到有 DHCP 服务且能访问互联网的上游 LAN 口。板子启动后连接热点即可，无需为客户端设置静态 IP。热点在未插网线时也会运行，但此时没有互联网转发。

日志出现以下内容表示有线 DHCP 和 NAPT 已准备好：

    router: WAN IP: ... mask: ... gateway: ...
    router_ap: AP gateway: ..., DNS: ...
    router: Internet forwarding ready (Ethernet -> Wi-Fi AP)

客户端成功取得地址后还会出现：

    router: LAN DHCP lease: ...

这些日志只表明板上接口已配置；客户端能否真正访问互联网仍需按下面的步骤验收。

## 组件管理

本项目已通过以下命令添加依赖，不需要在已有检出中重复执行：

    idf.py add-dependency "espressif/ethernet_init==1.4.1"

main/idf_component.yml 声明依赖，dependencies.lock 固定解析结果。idf.py reconfigure/build 自动下载到 managed_components，不要手工复制或修改该目录。

当前使用 ethernet_init 1.4.1，并通过其 Kconfig 条件依赖拉取 yt8531 0.2.0。Wi-Fi、DHCP、DNS 设置及 NAPT 使用 IDF 自带组件。更新受管依赖可运行 idf.py update-dependencies，更新后重新构建和上板验收。

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
- AP 使用 /24 私有子网。若与 WAN 重叠，依次尝试 192.168.4.1、10.42.0.1、172.20.0.1，保留当前可用网段。
- 检查同时考虑 WAN 和 LAN 的掩码，覆盖上游 /16、/8 等较大子网；所有候选均冲突时停止转发并报错。
- 上游取得 IPv4 地址后，将其主 DNS 或备用 DNS 下发给热点客户端；均未提供时使用配置的备用 DNS。
- 网关或 DNS 改变时，已有 Wi-Fi 客户端会被断开一次，以重新连接并取得正确租约。
- 拔网线或 WAN 丢失 IP 时关闭 NAPT，保留热点和本地 DHCP；重新取得 WAN 地址后恢复。
- 仅 DNS 改变而 IP/网关/掩码均不变的 DHCP 续租不会触发 IDF GOT_IP 事件；此时重新插拔网线或重启板子以同步 DNS。
- NAPT 不提供入站防火墙；能添加到 LAN 子网路由的上游设备仍可主动访问热点客户端。
- 当前提供 IPv4 NAT；未实现 IPv6 转发、管理网页或 OTA。NVS 初始化失败会报错，不会自动擦除已有 NVS。

## 验证

在有 C 编译器的机器上运行子网判断测试：

    cc -std=c11 -Wall -Wextra -Werror tests/test_subnet.c -o build/test_subnet
    ./build/test_subnet

Windows 可使用同等的宿主 C 编译器。ESP-IDF 的 RISC-V 交叉编译器生成的程序不能直接在 Windows 运行。

有实板和 pytest-embedded 环境时，可运行串口 / WAN ping 冒烟测试：

    pytest pytest_router.py --embedded-services esp,idf --target esp32s31 --port COMx

完整上网验收需要一台连接热点的手机或电脑：

1. 确认客户端通过 DHCP 获取当前 AP 子网地址、网关和上游 DNS。
2. 暂时禁用客户端其他互联网出口（例如手机移动数据），避免误判。
3. ping 当前 AP 网关，再访问外部 IP，随后打开 HTTPS 网站验证 DNS 和 TCP 转发。
4. 拔掉网线，确认热点仍可接入；重新插入后确认上网恢复。
5. 将上游设为与 192.168.4.0/24 重叠的网络，确认日志显示新的 AP 网段，客户端重新获取地址后仍可上网。

项目构建与软件测试不能证明 PHY 时序、实测吞吐量或以上完整上网流程。首次烧录前请确认板子 Flash 容量和串口；flash 会替换现有应用固件。

## 官方参考

- [CoreBoard 原理图](https://dl.espressif.com/schematics/esp32-s31-function-coreboard-1-schematics.pdf)
- [S31 支持状态](https://developer.espressif.com/hardware/esp32s31/)
- [ethernet_init 组件](https://components.espressif.com/components/espressif/ethernet_init/versions/1.4.1/readme)
- [ESP-NETIF API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/network/esp_netif_programming.html)
