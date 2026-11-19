# ESP32-C6-LCD-2.73 固件

[English](README.md)

本目录提供 ESP32-C6-LCD-2.73 的出厂固件，用于恢复产品的开箱演示。

## 固件文件

| 文件 | 用途 |
| --- | --- |
| `ESP32-C6-LCD-2.73_FactoryOnly_20260724.bin` | 出厂固件，恢复开箱演示。 |

文件名中的日期表示固件版本。出厂 BIN 是包含引导程序、分区表和应用程序的合并镜像，请将整个文件从 **0x0** 地址烧录。

## 烧录方法

**整片擦除 Flash 会清除设备中原有的固件、设置和用户数据，请提前备份需要保留的数据。**

### 使用 esptool

1. 安装 Python 和最新版 esptool：

   ```sh
   python -m pip install --upgrade esptool
   ```

2. 使用 USB 数据线将 ESP32-C6-LCD-2.73 连接至电脑，关闭占用设备串口的串口监视器。
3. 在本 `firmware/` 目录中打开终端，将以下命令中的 `<PORT>` 替换为设备实际串口号，例如 Windows 下的 `COM5` 或 Linux 下的 `/dev/ttyACM0`。
4. 先整片擦除 Flash，再烧录合并固件：

   ```sh
   python -m esptool --chip esp32c6 --port <PORT> erase-flash
   python -m esptool --chip esp32c6 --port <PORT> write-flash 0x0 ESP32-C6-LCD-2.73_FactoryOnly_20260724.bin
   ```

5. 烧录成功后，复位或重新上电，确认出厂演示正常启动，并检查各项功能是否符合预期。

如果工具无法连接，请按照产品说明让开发板进入下载模式，再重试。

### 使用乐鑫 Flash 下载工具

使用支持 ESP32-C6 的工具版本，选择 ESP32-C6 芯片和设备实际串口，通过 **ERASE** 整片擦除 Flash，再选择 `ESP32-C6-LCD-2.73_FactoryOnly_20260724.bin`，将烧录地址设为 **0x0**。开始烧录，等待成功后复位或重新上电。

命令详情请参考 [esptool 官方文档](https://docs.espressif.com/projects/esptool/en/latest/esp32c6/esptool/basic-commands.html)。
