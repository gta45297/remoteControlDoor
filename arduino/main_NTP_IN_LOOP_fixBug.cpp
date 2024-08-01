//V2
//1. 當CTRL按太久失效的時候 他會連帶NET不能下指令
//2. CTRL斷電後 會常按STOP 所以我要加上常按STOP太久會暫時失效的功能
//3. act>1的時候也會斷電，不收Ctrl的訊號
//4. 進入斷電的時間縮短為5s
//** 離開LOCK-MODE後，會打很短的STOP是Ctrl行為 不處理**

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "ESP8266TimerInterrupt.h"
#include <WiFiUdp.h>
#include <TimeLib.h>

//WIFI
const char* ssid="L";               //WiFi SSID
const char* password="Asd04678L";                //WiFi password

//NTP
unsigned int localPort=2390;   //local port to listen for UDP packets
IPAddress timeServerIP;    //time.nist.gov NTP server address
const char* ntpServerName="time.nist.gov"; //NTP Server host name
const int NTP_PACKET_SIZE=48;    // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE];   //buffer to hold incoming and outgoing packets
WiFiUDP udp;   //UDP instance to let us send and receive packets over UDP
#define timezone 8

//控制器
//可拉升電阻的GPIO
// O:D1 D2 D3 D5 D6 D7 X:D0 D8 D4是內建LED共用 有也沒用
// 可正常output
// O:D0 D1 D2 D3 D5 D6 D7 D8
#define CTRL_UP D3 //D2
#define CTRL_UP_MODE !
#define CTRL_STOP D2// D3一開始接地會造成不能開 //D3
#define CTRL_STOP_MODE 
#define CTRL_DOWN D5 //D5
#define CTRL_DOWN_MODE !
int ctrlIsDoing = false;//控制器控門
int netIsDoing = false; //網路控門
#define CTRL_EN D1 //D1
int ctrlEn = true;
#define ALLOW_CTRL_PRESS_TIME ( 10 * 1000 ) //10s
unsigned int savedPressTime = 0;
#define ENTER_LOCK_MODE_TIME ( 5 * 1000 ) // 5s
int lock_mode = false;

//遙控器
#define REMOTE_UP D6 //D6
#define REMOTE_UP_MODE !
#define REMOTE_STOP D7 //D7
#define REMOTE_STOP_MODE !
#define REMOTE_DOWN D0//D0 //D8 OUTPUT會讓SOC不能開 D8
#define REMOTE_DOWN_MODE !

 //網路按鈕的壓住時間
int net_press_time = 1000;//毫秒

//POST server
ESP8266WebServer server(80); // HTTP服务器在端口80
#define POST_PWD "Rtj01256F"

unsigned char saveTime;
//INTR
#if !defined(ESP8266)
  #error This code is designed to run on ESP8266 and ESP8266-based boards! Please check your Tools->Board setting.
#endif
// Select a Timer Clock
#define USING_TIM_DIV1                false           // for shortest and most accurate timer
#define USING_TIM_DIV16               false           // for medium time and medium accurate timer
#define USING_TIM_DIV256              true            // for longest timer but least accurate. Default
//計時多久觸發中斷
#define TIMER_INTERVAL_MS        100
// Init ESP8266 timer 1
ESP8266Timer ITimer;

// LOG
// 定义日志缓冲区的大小
const int LOG_BUFFER_SIZE = 20;
// 定义存储日志条目的数组
String logBuffer[LOG_BUFFER_SIZE];
// 指示下一个日志条目应该存储在哪个数组索引的指针
int logIndex = 0;

// 避免log打太多, ctrl只有壓鑄的第一次才會打
bool firstLog = false;

// build-in LED
#define LED_BUILTIN       2
#define LED_BUILTIN_MODE !

enum INIT_STATUS
{
    INIT_STATUS,
    CTRLEN_STATUS,
};
enum INIT_STATUS initStatus = INIT_STATUS;

String addLogTime( String entry )
{
    // 使用TimeLib.h库函数获取当前日期和时间
    int yr = year();
    int mnth = month();
    int dy = day();
    int hr = hour();
    int min = minute();
    int sec = second();

    // 格式化日期和时间字符串
    String dateStr = String(yr) + "-" + 
                    (mnth < 10 ? "0" : "") + String(mnth) + "-" + 
                    (dy < 10 ? "0" : "") + String(dy);
    String timeStr = (hr < 10 ? "0" : "") + String(hr) + ":" + 
                    (min < 10 ? "0" : "") + String(min) + ":" + 
                    (sec < 10 ? "0" : "") + String(sec);

    // 创建带有时间戳的日志条目
    return dateStr + " " + timeStr + " - " + entry;
}

// 向日志添加一条新条目的函数
void addLogEntry(String entry) {
  // 存储新的日志条目
  logBuffer[logIndex] = addLogTime( entry );
  // 更新索引，准备下一个条目的存储
  logIndex = (logIndex + 1) % LOG_BUFFER_SIZE;
}

// 打印所有日志条目的函数
void printLog() {
  Serial.println("Log Entries:");
  for (int i = 0; i < LOG_BUFFER_SIZE; i++) {
    if (logBuffer[i] != "") {
      Serial.println(logBuffer[i]);
    }
  }
}

struct TimerTask {
  void (*function)();  // 更正为函数指针
  unsigned long currentMillis;
  unsigned long wantDeltaMillis;
  TimerTask *nextTimeTask;
};

TimerTask* head = nullptr;

void add_timer(void (*newTask)(), unsigned long delay) {  // 参数更名为newTask，类型更正
  TimerTask* task = new TimerTask{newTask, millis(), delay, nullptr};  // 创建新任务

  if (head == nullptr) {
    head = task;  // 直接赋值给head
  } else {
    TimerTask *current = head;
    while (current->nextTimeTask != nullptr) {
      current = current->nextTimeTask;
    }
    current->nextTimeTask = task;
  }
}

void timer_isr() {
  unsigned long sysTime = millis();
  TimerTask **ptr = &head;
  while (*ptr != nullptr) {
    TimerTask *current = *ptr;
    unsigned long currentDelta = sysTime - current->currentMillis;

    if(currentDelta >= current->wantDeltaMillis) {  // 更正时间比较逻辑
      current->function();  // 执行任务
      *ptr = current->nextTimeTask;  // 从链表中断开
      delete current;  // 删除当前任务
    } else {
      ptr = &current->nextTimeTask;  // 移动到下一个任务
    }
  }
}

void blinkLed_isr()
{
    static bool toggle = false;
    digitalWrite( LED_BUILTIN, toggle );
    toggle = !toggle;

    switch( initStatus )
    {
        case INIT_STATUS:
            add_timer( blinkLed_isr, 100 );
            break;
        case CTRLEN_STATUS:
            if( ctrlEn )
            {   Serial.println("A");
                digitalWrite( LED_BUILTIN, LED_BUILTIN_MODE true );
            }else
            {   Serial.println("B");
                digitalWrite( LED_BUILTIN, LED_BUILTIN_MODE false );
            }
            
            break;            
    }
}    

void log_init()
{
    Serial.begin(115200);
    Serial.println("ESP8266 start");
}

// WiFi事件处理函数
void onWiFiEvent(WiFiEvent_t event) {
    switch(event) {
        case WIFI_EVENT_STAMODE_GOT_IP:
            Serial.println("WiFi connected");
            addLogEntry( "WiFi connected" );
            initStatus = CTRLEN_STATUS; //回歸標示ctrl是否開啟使用
            Serial.println("IP address: ");
            Serial.println(WiFi.localIP());
            break;
        case WIFI_EVENT_STAMODE_DISCONNECTED:
            Serial.println("WiFi lost connection");
            addLogEntry( "WiFi lost connection" );
            initStatus = INIT_STATUS;
            blinkLed_isr();
            // 在这里可以添加重新连接的逻辑
            break;
    }
}

void wifi_init()
{
    // 设置WiFi事件处理函数
    WiFi.onEvent(onWiFiEvent);

    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
}
//不能在中斷處理網路 避免衝突 WDT，只設flag
bool requireUpdateNTP = false;

enum NtpState {
  IDLE,
  WAITING_FOR_RESPONSE,
};
NtpState ntpState = IDLE;
unsigned long ntpRequestTime = 0;

void updateSystimeFromNTP()
{
  requireUpdateNTP = true;
}

void ntp_init()
{
    //Start UDP
    Serial.println("Starting UDP");
    udp.begin(localPort);
    Serial.print("Local port: ");
    Serial.println(udp.localPort());

    updateSystimeFromNTP();
}

void sendNtpPacket(const char* address) {
  udp.beginPacket(address, 123); // NTP请求端口为123
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;            // Stratum, or type of clock
  packetBuffer[2] = 6;            // Polling Interval
  packetBuffer[3] = 0xEC;         // Peer Clock Precision
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();

  ntpState = WAITING_FOR_RESPONSE;
  ntpRequestTime = millis(); // 记录请求时间
}

// 尝试获取NTP时间，以非阻塞方式
int tryGetNtpTime() {
  if (ntpState == WAITING_FOR_RESPONSE) {
    if (millis() - ntpRequestTime > 1500) { // 超时检查
      ntpState = IDLE; // 重置状态
      Serial.println("NTP request timed out.");
      return -1;
    }
    
    int size = udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      udp.read(packetBuffer, NTP_PACKET_SIZE); // 读取响应包
      unsigned long secsSince1900;
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      
      time_t time = secsSince1900 - 2208988800UL + timezone * SECS_PER_HOUR; // 转换时间
      // 处理时间...

      setTime( time );
      ntpState = IDLE; // 重置状态
      requireUpdateNTP = false;
      Serial.print("NTP time received: ");
      Serial.println(time);

      return 1;
    }
  }
  return 0;
}

void setSystemTime( time_t inputTime )
{
  setTime( inputTime );
}

void IRAM_ATTR intr_isr()
{
    timer_isr();
#if (TIMER_INTERRUPT_DEBUG > 0)
  Serial.print("Delta ms = ");
  Serial.println(millis() - lastMillis);
  lastMillis = millis();
#endif
}

void intr_init()
{
    // Interval in microsecs
    if( ITimer.attachInterruptInterval( TIMER_INTERVAL_MS * 1000, intr_isr ) )
    {
        Serial.print(F("Starting  ITimer"));
    }
    else
    {
        Serial.println(F("Can't set ITimer correctly. Select another freq. or interval"));
        while(1);
    }
}

void ctrlPressUp()
{   Serial.println("ctrlPressUp");
    if( firstLog )
        addLogEntry( "ctrlPressUp" );
    digitalWrite( REMOTE_UP, REMOTE_UP_MODE true );
}

void ctrlPressStop()
{   Serial.println("ctrlPressStop");
    if( firstLog )
        addLogEntry( "ctrlPressStop" );
    digitalWrite( REMOTE_STOP, REMOTE_DOWN_MODE true );
}

void ctrlPressDown()
{   Serial.println("ctrlPressDown");
    if( firstLog )
        addLogEntry( "ctrlPressDown" );
    digitalWrite( REMOTE_DOWN, REMOTE_STOP_MODE true );
}

void stopAllCtrlAction()
{
    Serial.println("stopAllCtrlAction");
    addLogEntry( "stopAllCtrlAction" );
    digitalWrite( REMOTE_UP, REMOTE_UP_MODE false );
    digitalWrite( REMOTE_STOP, REMOTE_STOP_MODE false );
    digitalWrite( REMOTE_DOWN, REMOTE_DOWN_MODE false );
}

enum DOOR_MODE
{
    NONE,
    UP,
    STOP,
    DOWN,
};

enum DOOR_MODE previous_door_status = NONE;
//透過控制器開關門
void handleCtrlDoor()
{
    int act = 0;
    enum DOOR_MODE door_status = NONE;// 0
    if( digitalRead( CTRL_UP ) == CTRL_UP_MODE true )
    {
        act++;
        door_status = UP;
    }
    if( digitalRead( CTRL_STOP ) == CTRL_STOP_MODE true )
    {
        act++;
        door_status = STOP;
    }
    if( digitalRead( CTRL_DOWN ) == CTRL_DOWN_MODE true )
    {
        act++;
        door_status = DOWN;
    }

    if( act > 1 )
    {
        Serial.print("ctrl signal error,act>1 ");
        Serial.print( digitalRead( CTRL_UP )  );
        Serial.print( digitalRead( CTRL_STOP ) );
        Serial.println( digitalRead( CTRL_DOWN ) );
        String regValueLog = "ctrl signal error,act>1 " 
          + digitalRead( CTRL_UP )
          + digitalRead( CTRL_STOP )
          + digitalRead( CTRL_DOWN );
        addLogEntry( regValueLog );

        stopAllCtrlAction();
        ctrlEn = false;
        ctrlIsDoing = false;
        blinkLed_isr(); //去關閉LED標示關閉該功能
    }

    if( act == 0 )
    {   //net開關門才不會被這個得長期rlease卡住
        if( ctrlIsDoing )
        {
            ctrlIsDoing = false;
            //通通不按
            stopAllCtrlAction();
            
            //放掉後 設空，以避免連續點放同一個鈕 會被誤算成壓住
            previous_door_status = NONE;
        }
        return;
    }
    
    //來避免net也跑進來做
    ctrlIsDoing = true;

    //避免打太多log
    firstLog = false;

    //避免電路脫落 CTRL壓10秒就不再接收訊號
    if( door_status != NONE )
    {
      if( previous_door_status == door_status )
      {
        unsigned int ctrlPressTime = millis() - savedPressTime;
        
        if( ctrlPressTime > ALLOW_CTRL_PRESS_TIME )
        {
          Serial.println("Ctrl press too long, disable!");
          addLogEntry( "Ctrl press too long, disable!" );
          stopAllCtrlAction();
          ctrlEn = false;
          ctrlIsDoing = false;
          blinkLed_isr(); //去關閉LED標示關閉該功能
          return;
        }

        // lock mode
        if( ctrlPressTime > ENTER_LOCK_MODE_TIME )
        {
          //加上斷電的處理
          if( door_status == STOP )
          {
            Serial.println("Ctrl enter LOCK-MODE");
            addLogEntry( "Ctrl enter LOCK-MODE" );
            lock_mode = true;
            ctrlIsDoing = false; //讓網路可控
            stopAllCtrlAction(); //確保NET可以正常控制

            previous_door_status = NONE;//確保下次進來不會因為STOP按鈕還沒穩定又瞬間進一次
            return;       
          }
        }        
      }else
      {
        firstLog = true;
        previous_door_status = door_status;
        savedPressTime = millis();
      }
    }
    // else
    // {
    //   //放掉後 設空，以避免連續點放同一個鈕 會被誤算成壓住
    //   //不會跑入這 前面act == 0 已經處理 return 了
    //   previous_door_status = NONE;
    // }
    
    // 假設門因為net的關係已經在壓了，先取消
    if( netIsDoing )
    {
        stopAllCtrlAction();
    }

    switch( door_status )
    {
        case UP:
            ctrlPressUp();
            break;
        case STOP:
            ctrlPressStop();
            break;
        case DOWN:
            ctrlPressDown();
            break;
    }
}

void gpio_init()
{
    //控制器
    // 将引脚2、3和4配置为输入模式，并启用内部上拉电阻
    pinMode( CTRL_UP, INPUT_PULLUP );
    pinMode( CTRL_STOP, INPUT_PULLUP );
    pinMode( CTRL_DOWN, INPUT_PULLUP );

    //遙控器
    pinMode( REMOTE_UP, OUTPUT );
    pinMode( REMOTE_DOWN, OUTPUT );
    pinMode( REMOTE_STOP, OUTPUT );
    //把所有控制給清掉，WDT reset時可能有按鈕還在壓。
    stopAllCtrlAction();

    //LED
    pinMode(LED_BUILTIN, OUTPUT);
    blinkLed_isr();

    // 外部關閉Ctrl D0
    pinMode( CTRL_EN, INPUT_PULLUP );
}

//網路按鈕的 直接設一個時間放開即可
void netUpRelase()
{   Serial.println("netUpRelase");
    addLogEntry( "netUpRelase" );
    netIsDoing = false;
    Serial.println( millis() - saveTime );
    digitalWrite( REMOTE_UP, REMOTE_UP_MODE LOW );
}

void netUpPress()
{   Serial.println("netUpPress");
    addLogEntry( "netUpPress" );
    netIsDoing = true;
    digitalWrite( REMOTE_UP, REMOTE_UP_MODE HIGH );
    saveTime = millis();

    //下次再放開
    add_timer( netUpRelase, net_press_time  );
}

void netDownRelase()
{   Serial.println("netDownRelase");
    addLogEntry( "netDownRelase" );
    netIsDoing = false;
    digitalWrite( REMOTE_DOWN, REMOTE_DOWN_MODE LOW );
}

void netDownPress()
{   Serial.println("netDownPress");
    addLogEntry( "netDownPress" );
    netIsDoing = true;
    digitalWrite( REMOTE_DOWN, REMOTE_DOWN_MODE HIGH );

    //下次再放開
    add_timer( netDownRelase, net_press_time  );
}

void netStopRelase()
{   Serial.println("netStopRelase");
    addLogEntry( "netStopRelase" );
    netIsDoing = false;
    digitalWrite( REMOTE_STOP, REMOTE_STOP_MODE LOW );
}

void netStopPress()
{   Serial.println("netStopPress");
    addLogEntry( "netStopPress" );
    netIsDoing = true;
    digitalWrite( REMOTE_STOP, REMOTE_STOP_MODE HIGH );

    //下次再放開
    add_timer( netStopRelase, net_press_time  );
}

// void handleRoot() {
//   String message = "Hello, World!";
//   server.send(200, "text/plain", message);
// }

void handleNotFound() {
  IPAddress remoteIp = server.client().remoteIP();
  Serial.print("Invalid cmd from ");
  Serial.println(remoteIp.toString());

  String logMessage = "Invalid cmd from ";
  logMessage += remoteIp.toString();
  addLogEntry(logMessage);

  // // 如果你还想发送一个HTTP响应给客户端
  // String message = "File Not Found\n\n";
  // message += "URI: ";
  // message += server.uri();
  // message += "\nMethod: ";
  // message += (server.method() == HTTP_GET) ? "GET" : "POST";
  // message += "\nArguments: ";
  // message += server.args();
  // message += "\nFrom IP: ";
  // message += remoteIp.toString();
  // message += "\n";

  // server.send(404, "text/plain", message);
}

String webLog()
{
    String outputLog = "";
    int index = logIndex;
    int i = LOG_BUFFER_SIZE;

    while( i )
    {
        i--;

        outputLog += logBuffer[ index ];
        outputLog += "$$";
        index = ( index + 1 ) % LOG_BUFFER_SIZE;
    }

    return "##" + outputLog;
}

void handleDoorOperation() {
  String want = server.arg("want"); // 获取参数"want"
  String pwd = server.arg("pwd"); // 获取参数"pwd"
  String pressTimeStr = server.arg("time"); // 获取参数"pwd"

  if (pwd != POST_PWD) { // 检查密码是否正确
    // server.send(401, "text/plain", "Unauthorized");
    Serial.println("PWD is incorrect.");
    addLogEntry( "PWD is incorrect." );
    return;
  }

  int pressTime = pressTimeStr.toInt();
  if ( pressTimeStr == " " || ( pressTime < 0 || pressTime > 10 * 1000 ) )
  {
    Serial.println("time is incorrect.");
    addLogEntry( "time is incorrect." );
    return;
  }

  if( ctrlIsDoing )
  {
    Serial.println("Ctrl is doing. Abort net cmd.");
    addLogEntry( "Ctrl is doing. Abort net cmd." );
    //ctrl的優先權最高 等他做完才能做
    server.send(200, "text/plain", "Ctrl is doing" + webLog() );
    return;
  }

    //有按鈕正在按，就不發
    if( netIsDoing )
    {
        Serial.println("net is busy.");
        server.send(200, "text/plain", "Busy" + webLog() );
        return;
    }

  net_press_time = pressTime;

  // 根据want参数执行相应操作
  if (want == "up") {
    // 执行上升操作
    server.send(200, "text/plain", "Door Going Up" + webLog() );
    addLogEntry( "Door Going Up" );
    netUpPress();
  } else if (want == "down") {
    // 执行下降操作
    server.send(200, "text/plain", "Door Going Down" + webLog() );
    addLogEntry( "Door Going Down" );
    netDownPress();
  } else if (want == "stop") {
    // 执行下降操作
    server.send(200, "text/plain", "Door Stop" + webLog() );
    addLogEntry( "Door Stop" );
    netStopPress();
  } else if (want == "none") {
    // 执行下降操作
    server.send(200, "text/plain", "Show log" + webLog() );
  } else {
    // 如果want参数不是预期的值
    // server.send(400, "text/plain", "Invalid Request");
    Serial.println("invalid opeation");

    // time ip 錯誤
    return;
  }
}

void postServer_init()
{
    // 打印ESP8266的IP地址
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    //server.on("/", HTTP_GET, handleRoot); hello world
    server.on("/door", HTTP_GET, handleDoorOperation); // 处理门操作
    server.onNotFound(handleNotFound);

    server.begin(); // 启动服务器
}

bool checkCtrlEnBit()
{
    return !digitalRead( CTRL_EN );
}

bool checkLeaveLockMode()
{   
    if( digitalRead( CTRL_STOP ) == CTRL_STOP_MODE true )
    {
      return false;
    }
    return true;
}

void setup() {
    log_init();

    intr_init();

    gpio_init();

    wifi_init();

    ntp_init();

    postServer_init();
}

void loop() {
    //post server
    server.handleClient();

    //控制器
    if( ctrlEn && checkCtrlEnBit() && !lock_mode )
    {
        handleCtrlDoor();
    }else
    {
        if( lock_mode )
        {
            if( checkLeaveLockMode() )
            {
                Serial.println( "Ctrl leave LOCK-MODE" );
                addLogEntry( "Ctrl leave LOCK-MODE" );
                lock_mode = false;
            }
        }else if( ctrlEn )
        {
            Serial.println("Ctrl disable");
            addLogEntry( "Ctrl disable" );
            ctrlEn = false;
            blinkLed_isr(); //去關閉LED標示關閉該功能
        }
    }
  
  // NTP 要求更新
  if( requireUpdateNTP == true )
  {
    requireUpdateNTP = false;

    sendNtpPacket(ntpServerName);
    ntpState = WAITING_FOR_RESPONSE;
  }else
  {
    // NTP 結果，並定時下次更新時間
    int ntpStatus = tryGetNtpTime();
    if( ntpStatus == -1 )
    {
      Serial.println("Get NTP time fail");
      addLogEntry( "Get NTP time fail" );
      add_timer( updateSystimeFromNTP, 10 * 60 * 1000); //10m
    }else if( ntpStatus == 1 )
    {
      Serial.println("Get NTP time success");
      addLogEntry( "Get NTP time success" );
      add_timer( updateSystimeFromNTP, 6 * 60 * 60 * 1000); //6h
    }
  }
}