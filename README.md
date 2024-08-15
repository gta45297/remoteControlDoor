# APP遠端開關鐵門系統
APP遠端開關鐵門系統  
  當家裡有包裹需要收，但所有人都要上班，沒有人可以收。這個時候可以使用遠端APP遙控鐵門開門。

## 和現有套件對比優勢:  
1. 所有code都是屬於open source, 可以避免洩漏相關資訊給外人  
2. 結合傳統鐵門控制器，GPIO可以串接傳統鐵門控制器，不用破解舊的滾碼遙控器
3. 傳統控制器/ APP遠端控制同時執行時，傳統控制器的優先權大於APP指令，以避免緊急事故  
4. GPIO含EN腳位，可以拉掉控制線，解除接收傳統鐵門控制器訊號，只接收APP下發開關門訊號  
5. APP可看到20筆歷史LOG紀錄，紀錄發送開關門時間、IP、來源、WIFI連線紀錄
6. NTP定時連網更新，能看到正確控制鐵門時間點

## 缺:  
1. arduino(WEMOS)、inventor平台限制，沒有SSH防護，需透過額外防火牆防護  
2. 老家只有社區鐵門，鐵門可能被其他住戶開關，無法詳測繼電器(按遙控器用)穩定度  

展示：(點圖連結至 youtube)
[![](http://https://github.com/gta45297/remoteControlDoor/blob/main/wired.jpg)](https://youtu.be/gIGYrbLZmgw "")
