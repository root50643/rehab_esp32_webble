# 建置、備份與燒錄

固定工具鏈：Arduino CLI1.5.1、esp32:esp32@3.3.11、NimBLE-Arduino2.5.1、PowerShell7、Python3。桌面測試另需C++17編譯器及Node20+；詳見toolchain.json。

## 建置

```powershell
# 建議獨立Arduino data，避免電腦上其他core版本被選中
$env:ARDUINO_DIRECTORIES_DATA = Join-Path $env:LOCALAPPDATA 'RehabWebBleArduino15'
./tools/build.ps1 -InstallCore
```

非Windows請將ARDUINO_DIRECTORIES_DATA設為自己的可寫入目錄。已安裝正確工具鏈後不必再加-InstallCore。若工具不在PATH可傳 -ArduinoCli 與 -Python 完整路徑。

建置腳本會使用ASCII暫存路徑，強制所有C/C++ translation units載入tools/ble_build_config.h，使NimBLE source host使用5個連線與20個CCCD。僅在sketch定義宏不會改變host連線池；不要混用core內附BLEDevice與NimBLEDevice。

產物放build/artifacts，factory app限4MiB；NVS為0x9000/0x5000，0xE000–0xFFFF保留Arduino boot_app0，app從0x10000開始。沒有OTA、core dump或運動資料檔案系統。網頁不再嵌入韌體，修改web只需更新Pages。

## 備份與燒錄

使用外接CH340 USB-UART燒錄；原生GPIO19/20留給USB Host。先關閉序列監看並用arduino-cli board list確認連接埠；COM3只是範例。

```powershell
New-Item -ItemType Directory -Force backups
python -m esptool --chip esp32s3 --port COM3 read-flash 0 0x1000000 backups/original.bin
Get-FileHash backups/original.bin -Algorithm SHA256
./tools/flash.ps1 -Port COM3
./tools/monitor.ps1 -Port COM3
```

備份必須為16,777,216 bytes，保存SHA-256供還原核對。flash.ps1不自動備份，請先完成備份。還原可用esptool將完整原映像寫回0x0。不要提交backups或.bin，可能含裝置資料。

## 測試

```powershell
python tools/run_tests.py --sanitize
npm install
npx playwright install chromium
npm run test:browser
```

Windows可在WSL執行C++/Python，或指定CXX。browser test可用CHROMIUM_PATH指定已安裝的Chrome/Edge。host與mock測試不能代替實機驗收。

GitHub Pages使用Actions workflow，設定Pages source為GitHub Actions。靜態資源僅發布web/，不含韌體、備份或測試證據。
