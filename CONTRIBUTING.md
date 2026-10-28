# 開發與維護

本 repo 未指定開源授權，貢獻與再發布權限由擁有者另行確認。

1. 依 BUILD_AND_FLASH.md 安裝固定工具鏈，執行 host tests、browser mock 與完整韌體編譯。
2. GATT 變更同時修改 PROTOCOL.md、webble_protocol.h、protocol.js 與兩側測試。
3. 維持 GPIO/UART、心率 Central、CCID、Gateway 分層；callback 不執行 NVS 或機台控制。
4. 區分接收、送出、ACK、機台回報。保留時效、16-bit 原值、UID 前導零。
5. 不提交 build、backups、local、node_modules、真實卡號／心率／帳密或 Flash 備份。
6. 實機驗證記錄板型、接線、韌體雜湊、日期與結果；CI 和 mock 不當作實機通過。

只向 root50643/rehab_esp32_webble push，不改用來源 rehab_esp32 remote。
