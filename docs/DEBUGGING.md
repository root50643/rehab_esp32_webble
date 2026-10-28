# 診斷

115200序列監看顯示WebBLE就緒與事件名稱。頁面提供STATE、COMMAND/RESPONSE/EVENT記錄，沒有AP、HTTP或TCP入口。

阻力分為target_level（要求）、sent_level（已送UART）、level_ack_pending（等待checksum ACK）、reported_level（機台回報）。ok=true僅表示接受。機台state只由UART回報，不以GPIO脈衝推定。

heart.age_ms、machine.age28_ms、age3f_ms分開。未知／過期顯示—，0是有效值。心率須三筆通知才就緒。free_heap/min_free_heap、dropped_events、CCID errors、UART checksum/framing/timeouts可追查壓力問題。
