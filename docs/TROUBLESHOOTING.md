# 故障排除

- Web Bluetooth不可用：用HTTPS/localhost、Chrome/Edge並啟用藍牙；Safari/Firefox不支援。
- 找不到ESP32：確認新韌體與RehabWebBLE廣播；4個網頁peer已滿時停止廣播。
- 控制按鈕停用：先claim，其他控制者須release或逾時；停止脈衝完成前不能claim。
- 心率—：確認MAC、180D/2A37、三筆有效通知與10秒時效。
- RPM/扭力—：0x28/0x3F各自更新；其他UART封包不能延長時效。檢查電位、隔離與TX/RX。
- 阻力未變：檢查sent_level、ACK timeout、reported_level；0不下送。
- 低MTU變慢：正常降為1Hz；錯誤分片或慢端可能斷開。控制者失聯會停止，重連不重放舊命令。
- core版本錯誤：依BUILD_AND_FLASH.md使用獨立ARDUINO_DIRECTORIES_DATA，不更改其他專案版本。
