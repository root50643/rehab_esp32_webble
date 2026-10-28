# WebBLE v1 協定

## GATT

廣播名稱 `RehabWebBLE`，Service UUID `7e400001-b5a3-f393-e0a9-e50e24dcca9e`。其餘 UUID 使用相同尾碼：

| 前段 | 名稱 | 屬性 | Channel |
|---|---|---|---|
| 7e400002 | STATE | Notify | 1 |
| 7e400003 | EVENT | Notify | 2 |
| 7e400004 | COMMAND | Write with response | 3 |
| 7e400005 | RESPONSE | Notify | 4 |
| 7e400006 | INFO | Read，未分片 JSON | 5 |

先讀 INFO、訂閱 RESPONSE/EVENT/STATE，再送命令。INFO 含 v、protocol、firmware、max_web_peers、total_connections、max_json_bytes、max_command_bytes、lease_ms、heartbeat_ms、max_state_hz、peer_key、torque_writable。v/protocol 必須為1。

## UTF-8 JSON 分片

除 INFO Read，每個 GATT value 為8-byte標頭加 UTF-8 JSON：

| Offset | Bytes | 值 |
|---|---|---|
| 0 | 1 | magic A7 |
| 1 | 1 | version 01 |
| 2 | 1 | Channel |
| 3 | 1 | bit0 START、bit1 END，其餘為0 |
| 4 | 2 | message ID，little-endian |
| 6 | 2 | JSON byte offset，little-endian |
| 8 | 其餘 | 非空 payload |

每筆從 offset0 的 START 開始，offset 連續，最後一片 END；單片可同時 START/END。2秒為整筆期限，不由每片刷新。重複、亂序、錯誤 Channel、超長或非法 UTF-8 會拒絕；韌體對非法 COMMAND 分片斷開 peer。COMMAND 最多1024 bytes，其他最多4096 bytes。

GATT value 上限 min(MTU-3,244)。瀏覽器預設每片20 bytes，不假定 Web API 可取得 MTU。不同 characteristic 用獨立組裝器。通知不提供應用層可靠投遞保證；STATE 的 dropped_events 表示該連線事件佇列丟棄數，不重播離線事件。

## 命令與回覆

```json
{"v":1,"id":1,"cmd":"claim"}
{"v":1,"id":2,"cmd":"set_resistance","level":300}
{"v":1,"id":3,"cmd":"set_heart_rate_address","address":"AA:BB:CC:DD:EE:FF"}
```

id 為1..4294967295整數，每條連線遞增。最後重複 id 重送快取回覆，不重做控制；更舊 id 回 stale_request。新連線可從1開始，不重放未確認命令。GATT write 必須序列化，不交錯不同命令的分片。

| cmd | 權限／參數 | 行為 |
|---|---|---|
| get_state | 任何 peer | 排入最新 STATE |
| claim | 無持有者且未停止中 | 取得30秒租約 |
| heartbeat | 控制者 | 續租30秒 |
| release | 控制者 | 釋放並停止 |
| start | 控制者 | 排入 Start 脈衝 |
| stop | 控制者 | 優先停止，取消排隊 Start/阻力 |
| pause | 控制者 | 僅確認，不產生硬體暫停動作 |
| set_resistance | 控制者，level 0..65535整數 | >0發0x44；0取消待送目標但不下送 |
| set_heart_rate_address | 控制者且 idle，address MAC或空字串 | 保存NVS，套用心率目標 |
| scan_heart_rate | 控制者 | 排入掃描，完成後 heart_scan EVENT |

```json
{"v":1,"type":"response","id":2,"cmd":"set_resistance","ok":true,"peer_key":1,"lease":{"owner":1,"remaining_ms":29000,"you_control":true,"stopping":false}}
```

ok 表示接受，不代表硬體完成。錯誤以 ok:false,error 回報，包括 not_controller、controller_busy、stopping、machine_busy、invalid_level、invalid_address_or_storage_error、scan_busy、unknown_command、invalid_json_or_schema、stale_request。

## 狀態與事件

STATE：v:1、type:state、seq、uptime_ms、peer_key、lease、heart、machine、rfid、dropped_events、free_heap、min_free_heap。

- heart：bpm、age_ms、connected、subscribed、ready、battery、name、address、address_type、rssi、notifications、consecutive、disconnects、last_error。三筆有效非零通知後提供 BPM；10秒失效為 null。
- machine：state、state_age_ms、tracking、uart_online、age_ms、rpm、machine_bpm、minutes、seconds、distance_raw、calories_raw、watt28_raw、watt3f_raw、reported_level、torque_raw、age28_ms、age3f_ms、target_level、sent_level、level_ack_pending、level_queued、pulse、queued_controls、last_error，另有解析計數。0x28與0x3F各自5秒失效為 null，0是有效值。
- rfid：state、ready、card_present、uid、age_ms、cards、errors、last_error、vid、pid、features、max_message_length、last_sw。UID為大寫HEX，保留前導零；移卡清空。
- lease：owner為連線generation key或null，remaining_ms、you_control、stopping。一個控制者、多個唯讀；沒有身分驗證或bonding。

EVENT：v:1、type:event、seq、uptime_ms、event、data（選用）。包含 card（data.uid）、card_present、card_removed、heart_disconnected、heart_valid、heart_stale、reader_error、heart_scan。掃描最多顯示優先排序的12個候選，超過時 data.truncated:true。

## 機台 UART 與 USB

UART 57600/8N1，frame=`55 COMMAND LENGTH DATA CHECKSUM 90`，checksum=`FF - ((COMMAND + LENGTH + sum(DATA)) & FF)`。多位元組DATA為big-endian。

- 0x27/1：Idle=0、Start=1、Pause=2、End=3，原樣回覆。
- 0x28/12：分[0..1]、秒[2]、距離[3..4]、熱量[5..6]、心率[7]、RPM[8..9]、功率[10..11]。回覆data=原checksum。
- 0x30/0：啟動事件，回覆0x30/1/data=1。
- 0x3F/6：阻力[0..1]、功率[2..3]、扭力[4..5]。回覆data=原checksum。
- 0x44/4送出：阻力2bytes+00 00。收到0x44/1的對應checksum才確認ACK，3秒逾時；ACK不再被ACK。

Start GPIO4 HIGH500ms；Stop GPIO5 HIGH3000ms再LOW等待500ms。追蹤時UART31秒未有效回報會停止。CL-2000 VID1206/PID2107，沿用受限CCID/APDU、同卡去重與USB逾時恢復，不接受任意USB讀卡機。

未來遊戲可直接引用 web/protocol.js 的 UUID、command、chunkPayload、FrameReassembler，自行維護租約、write序列、裝置權限、斷線與null；不依賴測試頁UI。
