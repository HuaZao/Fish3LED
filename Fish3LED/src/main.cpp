#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <ESPAsyncWiFiManager.h>
#include <Preferences.h>

Preferences preferences;
AsyncWebServer server(80);
DNSServer dns;

// NTP客户端配置
WiFiUDP udp;
NTPClient ntpClient(udp, "pool.ntp.org", 8 * 3600);

// PWM 引脚
const int whitePwmPin = D1;  // 白光PWM引脚
const int bluePwmPin = D2;   // 蓝光PWM引脚
const int purplePwmPin = D4; // 紫光PWM引脚
const int fanPwmPin = D5; // 风扇PWM引脚
const int ntcPin = A0; // 风扇PWM引脚

// 热敏电阻参数
const float nominalResistance = 10000;  // 10K电阻
const float nominalTemperature = 25;    // 25摄氏度
const float betaCoefficient = 3950;     // B值

#define TEMP_HIGH 60 // 高温阈值
#define TEMP_LOW 40  // 低温阈值

// W,B,U
int pwmValuesPerHour[][3] = {
    {0, 0, 0},          // 0时
    {0, 0, 0},          // 1时
    {0, 0, 0},          // 2时
    {0, 0, 0},          // 3时
    {0, 0, 0},          // 4时
    {0, 0, 0},          // 5时
    {0, 0, 0},          // 6时
    {0, 0, 0},          // 7时
    {0, 0, 0},          // 8时
    {200, 200, 0},      // 9时
    {500, 500, 200},    // 10时
    {900, 900, 300},    // 11时
    {1000, 1000, 1000}, // 12时
    {1000, 1000, 800},  // 13时
    {900, 900, 500},    // 14时
    {800, 800, 300},    // 15时
    {700, 800, 300},    // 16时
    {600, 800, 300},    // 17时
    {500, 800, 300},    // 18时
    {400, 400, 250},    // 19时
    {300, 300, 300},    // 20时
    {0, 0, 0},          // 21时
    {0, 0, 0},          // 22时
    {0, 0, 0},          // 23时
};

//当前时间(小时)
int currentHour;
//温控模式0->自动,1->手动
int fanModel = 0;
int fanPwmValue = 0;
//预览模式,1秒切一个时间
bool isPreModel;
int currentPreCount = -1;

int getPwmPin(int color);
float readTemperature();
void handleRoot();
void savePwmToPreferences(int hour, int white, int blue, int purple);
void loadPwmFromPreferences();
void startPreModel();

void savePwmToPreferences(int hour, int white, int blue, int purple) {
  preferences.begin("pwmSettings", false); 
  preferences.putInt(("white" + String(hour)).c_str(), white);
  preferences.putInt(("blue" + String(hour)).c_str(), blue);
  preferences.putInt(("purple" + String(hour)).c_str(), purple);
  preferences.end(); 
}

void loadPwmFromPreferences(){
  for (int hour = 0; hour < sizeof(pwmValuesPerHour) / sizeof(pwmValuesPerHour[0]); hour++) {
    int white = preferences.getInt(("white" + String(hour)).c_str(), -1);
    int blue = preferences.getInt(("blue" + String(hour)).c_str(), -1);
    int purple = preferences.getInt(("purple" + String(hour)).c_str(), -1);
    if (white >= 0) {
      pwmValuesPerHour[hour][0] = white;
    }
    if (blue >= 0) {
      pwmValuesPerHour[hour][1] = blue;
    }
    if (purple >= 0) {
      pwmValuesPerHour[hour][2] = purple;
    }
  }
}

float readTemperature(){
   int rawValue = analogRead(ntcPin);
   const int referenceVoltage = 3300; 
  float voltage = (rawValue / 1023.0) * referenceVoltage;
  float resistance = ((referenceVoltage - voltage) * nominalResistance) / voltage;
  float steinhart = resistance / nominalResistance; 
  steinhart = log(steinhart);
  steinhart /= betaCoefficient;
  steinhart += 1.0 / (nominalTemperature + 273.15); 
  steinhart = 1.0 / steinhart;
  float temperature = steinhart - 273.15;
  return temperature;
}

void startPreModel(){ 
   Serial.println("进入预览模式!");
   currentPreCount = -1;
   isPreModel = true;
}

void setup()
{
  Serial.begin(9600);
  pinMode(whitePwmPin, OUTPUT);
  pinMode(bluePwmPin, OUTPUT);
  pinMode(purplePwmPin, OUTPUT);  
  pinMode(fanPwmPin, OUTPUT);
  pinMode(ntcPin, INPUT);
  analogWriteRange(1023);
  analogWriteFreq(1000);
  // 上电默认关闭
  analogWrite(whitePwmPin, 0);
  analogWrite(bluePwmPin, 0);
  analogWrite(purplePwmPin, 0);
  analogWrite(fanPwmPin, 0);

  loadPwmFromPreferences();
  AsyncWiFiManager wifiManager(&server, &dns);
  wifiManager.setTimeout(180);
  if(!wifiManager.autoConnect("Fish3LED")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.reset();
    delay(5000);
  }
  Serial.println("connected...yeey :)");
  ntpClient.begin();
  ntpClient.update();
  Serial.print(ntpClient.getHours());
  handleRoot();
  AsyncElegantOTA.begin(&server);
  server.begin();
  Serial.println("HTTP server started");
}

void loop(){
  //温控
  float temperature = readTemperature(); 
  Serial.print("当前温度: ");
  Serial.println(temperature);
  if(fanModel == 0){
    if (temperature > TEMP_HIGH) {
      analogWrite(fanPwmPin, 1000); 
    } else if (temperature < TEMP_LOW) {
      analogWrite(fanPwmPin, 0);   
    } else {
      int fanSpeed = map(temperature, TEMP_LOW, TEMP_HIGH, 0, 1000);
      Serial.print("风扇PWM值: ");
      Serial.println(fanSpeed);
      analogWrite(fanPwmPin, fanSpeed);
    }
  }else{
      analogWrite(fanPwmPin, fanPwmValue);
  }

  //通道PWM
  ntpClient.update();
  if(isPreModel){
     currentPreCount ++;
     currentHour = currentPreCount;
     if(currentPreCount >= 23){
         isPreModel = false;
         currentPreCount = -1;
         Serial.print("预览结束.....");
     }
  }else{
     currentHour = ntpClient.getHours();
  }
  int nextHour = (currentHour + 1) % 24;
  Serial.println("当前Hour: " + String(currentHour));
  if (currentHour >= 0 && currentHour < 24 && nextHour >= 0 && nextHour < 24) {
    int startValues[3] = {
        pwmValuesPerHour[currentHour][0], 
        pwmValuesPerHour[currentHour][1], 
        pwmValuesPerHour[currentHour][2]  
    };

    int endValues[3] = {
        pwmValuesPerHour[nextHour][0], 
        pwmValuesPerHour[nextHour][1], 
        pwmValuesPerHour[nextHour][2]  
    };

    int elapsedMinutes = ntpClient.getMinutes();
    float progress = float(elapsedMinutes) / 60.00; 
    if(isPreModel){
       progress = 1;
    }
    Serial.println("当前区间进度: " + String(progress) + "%");
    for (int i = 0; i < 3; i++) {
       int currentBrightness = 0;
       if(endValues[i] > startValues[i]){
          int value = endValues[i] - startValues[i];
          currentBrightness = int(progress * float(value)) + startValues[i];
          Serial.println("++++");
       }else if (endValues[i] < startValues[i]){
          int value = startValues[i] - endValues[i];
          currentBrightness = startValues[i] - int(progress * float(value));
          Serial.println("-------");
       }else{
          Serial.println("======");
          currentBrightness = startValues[i];
       }
      currentBrightness = constrain(currentBrightness, 0, 1000); 
      analogWrite(getPwmPin(i), currentBrightness);
      Serial.println("当前通道: " + String(i) + "PWM: " + String(currentBrightness));
    }
  }
  delay(1000);
}

int getPwmPin(int color)
{
  switch (color)
  {
  case 0:
    return whitePwmPin;
  case 1:
    return bluePwmPin;
  case 2:
    return purplePwmPin;
  default:
    return -1;
  }
}

void handleRoot()
{
  // 设置根路由处理函数
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      String html = "<html><head><meta charset=\"UTF-8\"></head><body>";
      html += "<script src=\"https://cdn.jsdelivr.net/npm/chart.js@4.3.3/dist/chart.umd.min.js\"></script>";
      html += "<form id='updateForm'>";
      html += "时间(0-23): <input type='number' name='hour'><br>";
      html += "白光(0-1000): <input type='number' name='white'><br>";
      html += "蓝光(0-1000): <input type='number' name='blue'><br>";
      html += "紫光(0-1000): <input type='number' name='purple'><br>";
      html += "风速(0-1000): <input type='number' name='fan'>设置为0为自动温控<br>";
      html += "<button type='button' onclick='performUpdate()'>Update</button>";
      html += "</form>";
      html += "<canvas id='myChart'></canvas>";
      html += "<script>";
      html += "var ctx = document.getElementById('myChart').getContext('2d');";
      html += "var myChart = new Chart(ctx, {";
      html += "  type: 'line',";
      html += "  data: {";
      html += "    labels: ['0', '1', '2', '3','4','5','6','7','8','9','10','11','12','13','14','15','16','17','18','19','20','21','22','23',],"; // 根据需要添加时间标签
      html += "    datasets: [{";
      html += "      label: '白',";
      html += "      data: ["; 
      for (int i = 0; i < sizeof(pwmValuesPerHour) / sizeof(pwmValuesPerHour[0]); i++) {
        if (i > 0) {
          html += ",";
        }
        html += pwmValuesPerHour[i][0];
      }
      html += "],";
      html += "      borderColor: 'rgba(16, 0, 233, 1)',";
      html += "      fill: true";
      html += "    }, {";
      html += "      label: '蓝',";
      html += "      data: ["; 
      for (int i = 0; i < sizeof(pwmValuesPerHour) / sizeof(pwmValuesPerHour[0]); i++) {
        if (i > 0) {
          html += ",";
        }
        html += pwmValuesPerHour[i][1];
      }
      html += "],";
      html += "      borderColor: 'rgba(223, 142, 252, 1)',";
      html += "      fill: true";
      html += "    }, {";
      html += "      label: '紫',";
      html += "      data: [";
      for (int i = 0; i < sizeof(pwmValuesPerHour) / sizeof(pwmValuesPerHour[0]); i++) {
        if (i > 0) {
          html += ",";
        }
        html += pwmValuesPerHour[i][2];
      }
      html += "],";
      html += "      borderColor: 'rgba(75, 192, 192, 1)',";
      html += "      fill: true";
      html += "    }]";
      html += "  },";
      html += "  options: {";
      html += "    title: {";
      html += "        display: true,";
      html += "        text: \"ESP-3路恒流调光驱动\"";
      html += "      },";
      html += "    scales: {";
      html += "      y: {";
      html += "        beginAtZero: true,";
      html += "        max: 1000"; 
      html += "      }";
      html += "    },";
      html += "    plugins: {";
      html += "      legend: {";
      html += "        display: true,";
      html += "        position: 'top'";
      html += "      },";
      html += "      annotation: {";
      html += "        annotations: [";
      html += "          {";
      html += "            type: 'line',";
      html += "            mode: 'vertical',";
      html += "            scaleID: 'x',";
      html += "            value: " + String(currentHour) + ",";
      html += "            borderColor: 'red',";
      html += "            borderWidth: 1";
      html += "            borderDash: [5, 5],";
      html += "          }";
      html += "        ]";
      html += "      }";
      html += "    }";
      html += "  }";
      html += "});";
      html += "function performUpdate() {";
      html += "  var formData = new FormData(document.getElementById('updateForm'));";
      html += "  var xhr = new XMLHttpRequest();";
      html += "  xhr.open('POST', '/update', true);";
      html += "  xhr.onreadystatechange = function() {";
      html += "    if (xhr.readyState === 4 && xhr.status === 200) {";
      html += "      location.reload();"; // 刷新页面
      html += "    }";
      html += "  };";
      html += "  xhr.send(formData);";
      html += "}";
      html += "</script>";
      html += "</body></html>";
      request->send(200, "text/html", html); });

  // 设置更新路由处理函数
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    if(request->arg("hour") != "" && request->arg("white") != ""){
        int hour = request->arg("hour").toInt();
        int white = request->arg("white").toInt();
        white = constrain(white, 0, 1000); 
        if (hour >= 0 && hour <= 23) {
            pwmValuesPerHour[hour][0] = white;
            savePwmToPreferences(hour, white, pwmValuesPerHour[hour][1], pwmValuesPerHour[hour][2]);
            Serial.println("Updated White PWM value: " + String(white));
        }
     }
    if(request->arg("hour") != "" && request->arg("blue") != ""){
        int hour = request->arg("hour").toInt();
        int blue = request->arg("blue").toInt();
        blue = constrain(blue, 0, 1000); 
        if (hour >= 0 && hour <= 23) {
          pwmValuesPerHour[hour][1] = blue;
          savePwmToPreferences(hour, pwmValuesPerHour[hour][0], blue, pwmValuesPerHour[hour][2]);
          Serial.println("Updated Blue PWM value: " + String(blue));
        }
    }
    if(request->arg("purple") != "" && request->arg("white") != ""){
        int hour = request->arg("hour").toInt();
        int purple = request->arg("purple").toInt();
        purple = constrain(purple, 0, 1000); 
        if (hour >= 0 && hour <= 23) {
          pwmValuesPerHour[hour][2] = purple;
          savePwmToPreferences(hour, pwmValuesPerHour[hour][0], pwmValuesPerHour[hour][1], purple);
          Serial.println("Updated Purple PWM value: " + String(purple));
        }
    }
    if(request->arg("fan") != ""){
      int fan = request->arg("fan").toInt();
      if(fan == 0){
        fanModel = 0;
      }else{
        fanModel = 1;
        fanPwmValue = fan;
      }
    }
  request->send(200, "text/plain", "UpdatedSuccess!"); });

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
     for (int hour = 0; hour <= 23; hour++) {
         savePwmToPreferences(hour, -1, -1, -1);
     }
    delay(500);
    ESP.reset();
  request->send(200, "text/plain", "<meta charset=\"UTF-8\">请关闭当前页面!!否则ESP会一直重置"); });

  server.on("/premodel", HTTP_GET, [](AsyncWebServerRequest *request){
    startPreModel();
  request->send(200, "text/plain", "<meta charset=\"UTF-8\">进入预览模式....."); });
}