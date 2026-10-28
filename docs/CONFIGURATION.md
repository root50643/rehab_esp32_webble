# 設定

從HTTPS Web BLE頁連線並取得控制權。機台停止、脈衝與阻力ACK完成後，填入心率MAC，或掃描並選候選後儲存；空字串停用中繼。須支援標準180D/2A37，輪換MAC的裝置可能需要重新選擇。

NVS namespace rehab_webble、key heart，有magic/version/CRC，只保存心率MAC與revision。不讀舊Wi-Fi設定，不保存UID、心率或運動紀錄；通知與刷新不寫NVS。

控制租約30秒，頁面每10秒續租。租約是操作仲裁，不是身分驗證，附近BLE客戶端可能讀值並在無控制者時claim。
