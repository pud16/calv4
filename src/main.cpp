#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include "font_ru.h"

// Hardware configuration VERIFIED on JC3248W535C diagnostic v2.
static constexpr int BACKLIGHT=1;
static constexpr uint8_t TOUCH_ADDR=0x3B;
static Arduino_DataBus *bus=new Arduino_ESP32QSPI(45,47,21,48,40,39);
static Arduino_GFX *panel=new Arduino_AXS15231B(bus,GFX_NOT_DEFINED,0,false,320,480);
static Arduino_Canvas *gfx=new Arduino_Canvas(320,480,panel,0,0,0);
static WebServer server(80);
static Preferences prefs;
static bool displayOK=false, portal=false, routesReady=false;
static int screenNo=0, monthShift=0, tzOffset=3;
static float weatherLat=59.9343f, weatherLon=30.3351f;
static String weatherCity="Санкт-Петербург";
static bool weatherReady=false;
static float temperature=0, apparent=0, windKmh=0;
static int weatherCode=-1, humidity=0;
static uint32_t weatherAttempt=0;
static int timerRemaining=25*60;
static bool timerRunning=false;
static uint32_t timerTick=0, lastDraw=0, lastTouch=0, lastConnect=0;
static String ssid, password;
static constexpr uint16_t BG=0x0863, PANEL=0x10C5, ACCENT=0x4DDF, WHITE=0xFFFF,
    SECOND=0xB596, MUTED=0x7BCE, GREEN=0x45EA, BORDER=0x2148, RED=0xF325;
static const char *months[]={"Январь","Февраль","Март","Апрель","Май","Июнь","Июль","Август","Сентябрь","Октябрь","Ноябрь","Декабрь"};
static const char *weekdays[]={"Вс","Пн","Вт","Ср","Чт","Пт","Сб"};
static const char *weekdayShort[]={"Пн","Вт","Ср","Чт","Пт","Сб","Вс"};

static uint32_t nextCp(const char *&p){
  uint8_t c=(uint8_t)*p++; if(c<128)return c;
  if((c&0xE0)==0xC0){uint8_t b=(uint8_t)*p++;return ((c&31)<<6)|(b&63);}
  if((c&0xF0)==0xE0){uint8_t b=(uint8_t)*p++,d=(uint8_t)*p++;return ((c&15)<<12)|((b&63)<<6)|(d&63);}
  return '?';
}
static const Glyph* findGlyph(uint32_t code){
  for(const auto &g:FONT14)if(g.code==code)return &g;
  for(const auto &g:FONT14)if(g.code=='?')return &g;
  return nullptr;
}
static int stringWidth(const String &s,int scale=1){
  int count=0;const char *p=s.c_str();while(*p){nextCp(p);++count;}return count*12*scale;
}
static void txt(int x,int y,const String &s,uint16_t color=WHITE,int scale=1){
  const char *p=s.c_str();while(*p){const Glyph*g=findGlyph(nextCp(p));if(g){
    for(int yy=0;yy<18;++yy){uint16_t bits=g->rows[yy];
      for(int xx=0;xx<12;++xx)if(bits&(1<<xx)){
        if(scale==1)gfx->drawPixel(x+xx,y+yy,color);
        else gfx->fillRect(x+xx*scale,y+yy*scale,scale,scale,color);
      }
    }
  }x+=12*scale;}
}
static void centered(int cy,const String &s,uint16_t color=WHITE,int scale=1){txt((320-stringWidth(s,scale))/2,cy,s,color,scale);}
static void rect(int x,int y,int w,int h,uint16_t color=PANEL){gfx->fillRoundRect(x,y,w,h,11,color);}
static void button(int x,int y,int w,int h,const String &s,bool chosen=false){
  rect(x,y,w,h,chosen?ACCENT:PANEL);
  txt(x+(w-stringWidth(s))/2,y+(h-18)/2,s,chosen?BG:WHITE);
}
static bool getNow(tm &t){time_t now=time(nullptr);if(now<1700000000)return false;
  now+=tzOffset*3600;gmtime_r(&now,&t);return true;}
static int daysInMonth(int year,int month){static const int d[]={31,28,31,30,31,30,31,31,30,31,30,31};
  return month==2&&((year%4==0&&year%100!=0)||year%400==0)?29:d[month-1];}
static void calendarScreen(){
  tm now={};bool valid=getNow(now);int year=valid?now.tm_year+1900:2026;int mon=valid?now.tm_mon+1:10;
  int sum=(mon-1)+monthShift;year+=(sum>=0?sum/12:(sum-11)/12);mon=((sum%12)+12)%12+1;
  txt(22,70,months[mon-1]+" "+String(year),WHITE,1);
  button(20,107,55,37,"<");button(245,107,55,37,">");
  txt(93,116,"МЕСЯЦ",MUTED);
  for(int c=0;c<7;++c)txt(18+43*c,165,weekdayShort[c],SECOND);
  tm first={};first.tm_year=year-1900;first.tm_mon=mon-1;first.tm_mday=1;first.tm_hour=12;
  mktime(&first);int start=(first.tm_wday+6)%7;
  int count=daysInMonth(year,mon);
  for(int day=1;day<=count;++day){int i=start+day-1,col=i%7,row=i/7;
    int x=14+col*43,y=197+row*35;
    bool today=valid&&monthShift==0&&day==now.tm_mday;
    if(today)gfx->fillRoundRect(x-4,y-5,36,29,7,ACCENT);
    String d=String(day);txt(x+(30-stringWidth(d))/2,y,d,today?BG:WHITE);
  }
  rect(18,415,284,38);centered(424,valid?"Сегодня: "+String(now.tm_mday)+" "+String(months[now.tm_mon]):"Подключите Wi-Fi",valid?SECOND:ACCENT);
}
static void clockScreen(){
  tm now={};bool valid=getNow(now);
  centered(105,"ТОЧНОЕ ВРЕМЯ",MUTED);
  if(valid){char buf[16];snprintf(buf,sizeof(buf),"%02d:%02d",now.tm_hour,now.tm_min);
    centered(170,buf,WHITE,3);snprintf(buf,sizeof(buf),"%02d секунд",now.tm_sec);
    centered(255,buf,ACCENT);
    centered(315,String(now.tm_mday)+" "+months[now.tm_mon]+" "+String(now.tm_year+1900),WHITE);
    centered(350,String(weekdays[now.tm_wday]),SECOND);
    gfx->drawRoundRect(28,402,264,41,10,BORDER);
    centered(414,WiFi.status()==WL_CONNECTED?"Синхронизация по Wi-Fi":"Время без сети",SECOND);
  }else{centered(174,"--:--",WHITE,3);centered(316,"Нет синхронизации",ACCENT);}
}
static void timerScreen(){
  centered(92,"ФОКУС-ТАЙМЕР",SECOND);
  int mins=timerRemaining/60,secs=timerRemaining%60;
  char b[12];snprintf(b,sizeof(b),"%02d:%02d",mins,secs);
  centered(164,b,WHITE,3);
  centered(259,timerRunning?"Идёт отсчёт":"На паузе",timerRunning?GREEN:SECOND);
  button(20,312,135,52,timerRunning?"Пауза":"Старт",true);
  button(165,312,135,52,"Сброс");
  button(20,380,135,45,"25 мин");button(165,380,135,45,"5 мин");
}
static String skyDescription(int code){
  if(code==0)return "Ясно";
  if(code<=3)return "Переменная облачность";
  if(code==45||code==48)return "Туман";
  if(code>=51&&code<=67)return "Дождь";
  if(code>=71&&code<=86)return "Снег";
  if(code>=95)return "Гроза";
  return "Облачно";
}
static bool jsonNumber(const String &json,const String &key,float &value){
  int k=json.indexOf("\""+key+"\"");if(k<0)return false;
  int colon=json.indexOf(':',k);if(colon<0)return false;
  int begin=colon+1;while(begin<(int)json.length()&&(json[begin]==' '||json[begin]=='\n'))++begin;
  if(begin>=(int)json.length()||(!isDigit(json[begin])&&json[begin]!='-'))return false;
  value=json.substring(begin).toFloat();return true;
}
static void fetchWeather(){
  if(WiFi.status()!=WL_CONNECTED)return;
  weatherAttempt=millis();
  WiFiClientSecure client;client.setInsecure(); // public weather data, no credentials transmitted
  HTTPClient http;
  String url="https://api.open-meteo.com/v1/forecast?latitude="+String(weatherLat,4)+"&longitude="+String(weatherLon,4)+"&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m&timezone=auto";
  if(!http.begin(client,url))return;
  http.setTimeout(4500);
  int code=http.GET();
  if(code==200){
    String body=http.getString();int c=body.indexOf("\"current\"");
    if(c>=0){String current=body.substring(c);float t,a,h,w,codeValue;
      if(jsonNumber(current,"temperature_2m",t)&&jsonNumber(current,"apparent_temperature",a)&&jsonNumber(current,"relative_humidity_2m",h)&&jsonNumber(current,"wind_speed_10m",w)&&jsonNumber(current,"weather_code",codeValue)){
        temperature=t;apparent=a;humidity=(int)h;windKmh=w;weatherCode=(int)codeValue;weatherReady=true;
      }
    }
  }
  http.end();
}
static void weatherScreen(){
  centered(74,"ПОГОДА",WHITE);
  centered(111,weatherCity.length()>22?weatherCity.substring(0,22):weatherCity,SECOND);
  rect(17,150,286,151);
  if(weatherReady){
    String degrees=String((int)roundf(temperature))+" C";
    centered(167,degrees,WHITE,3);
    centered(254,skyDescription(weatherCode),ACCENT);
    rect(17,316,286,93);
    txt(27,330,"Ощущается: "+String((int)roundf(apparent))+" C",SECOND);
    txt(27,358,"Влажность: "+String(humidity)+"%",SECOND);
    txt(27,383,"Ветер: "+String((int)roundf(windKmh))+" км/ч",SECOND);
  }else{
    centered(180,"Нет данных погоды",SECOND);
    centered(235,WiFi.status()==WL_CONNECTED?"Обновляем данные...":"Подключите Wi-Fi",ACCENT);
  }
  centered(423,"Город: через настройки Wi-Fi",MUTED);
}
static void statusScreen(){
  centered(78,"ИНФОРМАЦИЯ",WHITE);
  rect(14,116,292,195);
  txt(29,136,"СЕТЬ",ACCENT);
  txt(29,167,WiFi.status()==WL_CONNECTED?"Wi-Fi подключён":"Wi-Fi не подключён",WHITE);
  if(WiFi.status()==WL_CONNECTED){txt(29,196,"IP: "+WiFi.localIP().toString(),SECOND);
    txt(29,225,"Сигнал: "+String(WiFi.RSSI())+" dBm",SECOND);
  }else{txt(29,201,portal?"Точка: CALENDAR-SETUP":"Нет подключения",SECOND);
    txt(29,230,"Адрес: 192.168.4.1",SECOND);}
  txt(29,268,"Время: UTC"+String(tzOffset>=0?"+":"")+String(tzOffset),SECOND);
  button(20,339,135,45,"UTC -");button(165,339,135,45,"UTC +");
  button(20,395,280,41,"Включить Wi-Fi SETUP");
  centered(440,"Настройки: IP устройства",MUTED);
}
static void drawAll(){
  if(!displayOK)return;
  gfx->fillScreen(BG);
  txt(17,17,"SMART CALENDAR",ACCENT);
  String connection=WiFi.status()==WL_CONNECTED?"Wi-Fi OK":(portal?"SETUP":"OFFLINE");
  txt(308-stringWidth(connection),17,connection,SECOND);
  gfx->drawFastHLine(14,52,292,BORDER);
  switch(screenNo){case 0:calendarScreen();break;case 1:clockScreen();break;case 2:weatherScreen();break;case 3:timerScreen();break;default:statusScreen();}
  gfx->drawFastHLine(13,459,294,BORDER);
  const char *labels[]={"Дата","Часы","Небо","Тайм","Сеть"};
  for(int i=0;i<5;++i){int x=4+i*63;gfx->fillRoundRect(x,463,60,17,4,i==screenNo?ACCENT:PANEL);
    txt(x+(60-stringWidth(labels[i]))/2,463,labels[i],i==screenNo?BG:SECOND);}
  gfx->flush();
}
static bool readTouch(int &x,int &y){
  static const uint8_t cmd[8]={0xB5,0xAB,0xA5,0x5A,0,0,0,8};
  Wire.beginTransmission(TOUCH_ADDR);Wire.write(cmd,sizeof(cmd));
  if(Wire.endTransmission()!=0||Wire.requestFrom(TOUCH_ADDR,(uint8_t)8)!=8)return false;
  uint8_t d[8];for(auto &v:d)v=Wire.read();
  if(!d[1]||d[1]>4)return false;
  x=((d[2]&15)<<8)|d[3];y=((d[4]&15)<<8)|d[5];
  return x>=0&&x<320&&y>=0&&y<480;
}
static void makePortal(){
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("CALENDAR-SETUP","calendar123");portal=true;
  Serial.println("AP SETUP: http://192.168.4.1 password calendar123");
}
static void startWeb(){
  if(routesReady)return;routesReady=true;
  server.on("/",HTTP_GET,[]{
    String html="<!doctype html><html lang='ru'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font:17px sans-serif;max-width:430px;margin:30px auto;padding:14px;background:#0b1724;color:#fff}input,button{width:100%;padding:13px;margin:8px 0;box-sizing:border-box;font:inherit;border-radius:9px;border:0}button{background:#65d9ff;color:#051522}small{color:#abc}label{display:block;margin-top:15px}</style><h2>Calendar v4</h2><p>Настройки устройства</p><form method='POST' action='/save'><label>Домашний Wi-Fi<input name='s' value='"+ssid+"' maxlength='32'></label><label>Пароль Wi-Fi (оставь пустым, чтобы не менять)<input name='p' type='password'></label><label>Город<input name='city' value='"+weatherCity+"' maxlength='28'></label><label>Широта<input name='lat' type='number' step='0.0001' min='-90' max='90' value='"+String(weatherLat,4)+"'></label><label>Долгота<input name='lon' type='number' step='0.0001' min='-180' max='180' value='"+String(weatherLon,4)+"'></label><label>Часовой пояс UTC (например, Москва +3)<input name='tz' type='number' min='-12' max='14' value='"+String(tzOffset)+"'></label><button>Сохранить настройки</button></form><small>Для настройки без домашней сети подключись к CALENDAR-SETUP (пароль calendar123) и открой http://192.168.4.1</small></html>";
    server.send(200,"text/html; charset=utf-8",html);
  });
  server.on("/save",HTTP_POST,[]{
    String s=server.arg("s"),p=server.arg("p");
    if(s.length()>32||p.length()>63){server.send(400,"text/plain","Invalid Wi-Fi credentials");return;}
    String newCity=server.arg("city");if(newCity.length()>0&&newCity.length()<50){weatherCity=newCity;prefs.putString("city",newCity);}
    float lat=server.arg("lat").toFloat(),lon=server.arg("lon").toFloat();
    if(lat>=-90&&lat<=90&&lon>=-180&&lon<=180){weatherLat=lat;weatherLon=lon;prefs.putFloat("lat",lat);prefs.putFloat("lon",lon);weatherReady=false;weatherAttempt=0;}
    tzOffset=constrain(server.arg("tz").toInt(),-12,14);prefs.putInt("tz",tzOffset);
    bool wifiChanged=false;
    if(s.length()&&s!=ssid){ssid=s;password=p;wifiChanged=true;}
    else if(s.length()&&p.length()){ssid=s;password=p;wifiChanged=true;}
    if(wifiChanged){prefs.putString("ssid",ssid);prefs.putString("pass",password);}
    server.send(200,"text/html; charset=utf-8","<meta charset='utf-8'><h2>Сохранено</h2><p>Можно закрыть страницу.</p>");
    if(wifiChanged){WiFi.begin(ssid.c_str(),password.c_str());lastConnect=millis();}
    drawAll();
  });
  server.begin();
}
void setup(){
  pinMode(BACKLIGHT,OUTPUT);digitalWrite(BACKLIGHT,LOW);
  Serial.begin(115200);delay(800);Serial.println("CALENDAR V4 BOOT");
  Wire.begin(4,8);Wire.setClock(400000);
  digitalWrite(BACKLIGHT,HIGH);
  displayOK=gfx->begin(40000000);
  if(!displayOK){Serial.println("DISPLAY INIT FAILED");return;}
  Serial.println("DISPLAY INIT OK");
  prefs.begin("calendar",false);
  tzOffset=prefs.getInt("tz",3);
  ssid=prefs.getString("ssid","");password=prefs.getString("pass","");
  weatherCity=prefs.getString("city","Санкт-Петербург");
  weatherLat=prefs.getFloat("lat",59.9343f);weatherLon=prefs.getFloat("lon",30.3351f);
  if(ssid.length()){WiFi.mode(WIFI_STA);WiFi.begin(ssid.c_str(),password.c_str());lastConnect=millis();}
  else makePortal();
  startWeb();
  configTime(0,0,"pool.ntp.org","time.google.com","time.cloudflare.com");
  timerTick=millis();drawAll();
}
void loop(){
  if(!displayOK){delay(1000);return;}
  server.handleClient();
  if(!portal&&WiFi.status()!=WL_CONNECTED&&millis()-lastConnect>20000)makePortal();
  if(timerRunning&&millis()-timerTick>=1000){
    uint32_t seconds=(millis()-timerTick)/1000;timerTick+=seconds*1000;
    if(seconds>=(uint32_t)timerRemaining){timerRemaining=0;timerRunning=false;}
    else timerRemaining-=seconds;
  }
  if(WiFi.status()==WL_CONNECTED&&(weatherAttempt==0||millis()-weatherAttempt>1200000UL))fetchWeather();
  int x,y;
  if(readTouch(x,y)&&millis()-lastTouch>330){
    lastTouch=millis();Serial.printf("TOUCH x=%d y=%d screen=%d\n",x,y,screenNo);
    if(y>=455){screenNo=min(4,x/64);}
    else if(screenNo==0&&y>=100&&y<=152){if(x<80)monthShift--;else if(x>235)monthShift++;}
    else if(screenNo==3){
      if(y>=307&&y<375){if(x<160){timerRunning=!timerRunning;timerTick=millis();}else{timerRunning=false;timerRemaining=25*60;}}
      else if(y>=375&&y<=435){timerRunning=false;timerRemaining=x<160?1500:300;}
    }else if(screenNo==4&&y>=334&&y<=390){tzOffset=constrain(tzOffset+(x<160?-1:1),-12,14);prefs.putInt("tz",tzOffset);}
    else if(screenNo==4&&y>=390&&y<=446){makePortal();}
    drawAll();lastDraw=millis();
  }
  uint32_t refresh=(screenNo==1||screenNo==3)?1000:15000;
  if(millis()-lastDraw>refresh){lastDraw=millis();drawAll();}
  delay(35);
}
