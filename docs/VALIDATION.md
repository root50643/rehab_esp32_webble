# 驗證紀錄

2026-10-28（Asia/Taipei），新repo 0.1.0-webble。測試使用人工封包、UID與心率；未燒錄或操作真實機台。

| 項目 | 結果／證據 |
|---|---|
| 分割表 | Python unittest 8項通過 |
| 可攜C++ | UART、Pulse/FIFO、CCID、心率、MAC、WebBLE分片/UTF-8/租約；WSL g++ C++17，ASan/UBSan |
| Node | UUID同步、DataView/UTF-8、分片順序/超時/上限、schema/16-bit、freshness、安全呈現 |
| 真實瀏覽器mock | Edge headless，實際頁面＋假的BLE介面；通知、唯讀/claim、阻力300、null、斷線/重連、360/768/1440px無橫向溢出 |
| 韌體編譯 | Arduino CLI1.5.1、隔離core3.3.11、NimBLE-Arduino2.5.1，完整編譯及binary分割檢查通過 |
| GitHub CI/Pages | 以新repo Actions結果為準；本機通過不代表遠端已通過 |

browser mock只驗證前端，不證明作業系統BLE或ESP32無線功能。程式可編譯不等於外設驗收。

本機app為685,248 bytes／4,194,304 bytes，SHA-256 `32C05FB237CC009F8308BD58F8CC817B5264A7C1DCA35567141012E90ABD9BAF`。產物在忽略的build/artifacts，未燒錄至裝置。不同建置路徑或時間的binary雜湊可能不同。

## 必須完成的實機驗收

1. Windows Chrome/Edge與Android Chrome的裝置選擇、權限、重新連線與MTU差異。
2. ESP32同時保持心率Central及一個控制者＋至少兩個唯讀；上限4個網頁＋1個心率。
3. 兩個同時claim只成功一個；唯讀不能控制、改設定或掃描。
4. 控制者斷線／30秒無heartbeat／release時取消排隊Start與阻力，Stop GPIO量測HIGH3000ms＋LOW500ms，期間不能交接控制。
5. 心率三筆就緒、10秒失效、斷線/重連/public/random address；不讓舊值顯示有效。
6. UART 0x28/0x3F獨立5秒時效、16-bit值、checksum ACK與阻力目標/已送/回報差別。
7. CL-2000供電、UID前導零、同卡去重、移卡、USB拔插恢復，不重放離線事件。
8. 慢peer／低MTU／大量事件／連線churn／8小時運作；無持續heap下降、重啟或GPIO計時阻塞。
9. NVS心率位址重開機保存，運動中修改被拒絕；舊Wi-Fi記錄不被使用。

實測記錄需含日期、韌體SHA-256、板型、接線、供電、外設型號、預期與實際結果。原始Flash、真實UID與個人心率留在忽略目錄。
