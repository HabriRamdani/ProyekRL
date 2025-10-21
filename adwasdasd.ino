/*
 * Smart Energy Meter + Web Control + Adjustable Power Limit + QR WiFi Access
 * Board: ESP32 Dev Module
 */

#include <WiFi.h>
#include <WebServer.h>
#include <PZEM004Tv30.h>
#include <EEPROM.h>

#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
#define RELAY_PIN 2

const char* ssid = "LEKMELU GAWE YOMELU BAYAR";
const char* password = "cahrantau";

#define EEPROM_SIZE 32
#define ADDR_TRIP 0

PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
WebServer server(80);

float voltage = 0, current = 0, power = 0, energy = 0;
float avgPower = 0, cost = 0, TRIP_THRESHOLD = 1800.0;
bool relayStatus = true;
const unsigned long DEBOUNCE_TIME = 3000;
unsigned long lastTripTime = 0;

String getPage() {
  // Format QR Code untuk WiFi (kompatibel dengan HP Android/iPhone)
  String wifiQR = "WIFI:T:WPA;S:" + String(ssid) + ";P:" + String(password) + ";;";
  String qrURL = "https://chart.googleapis.com/chart?chs=200x200&cht=qr&chl=" + wifiQR;

  String html = R"rawliteral(
  <html>
  <head>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>Smart Energy Dashboard</title>
    <script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
    <style>
      body { font-family: Arial; background:#f9fafb; text-align:center; margin:0; }
      h2 { color:#1e293b; margin:20px 0; }
      .card { background:white; border-radius:12px; width:80%; margin:20px auto; padding:15px;
              box-shadow:0 2px 6px rgba(0,0,0,0.1); text-align:center; }
      canvas { display:block; margin:auto; width:80%; height:130px !important; }
      button { padding:8px 16px; border:none; border-radius:6px; margin:5px; font-size:15px; }
      .on { background:green; color:white; }
      .off { background:red; color:white; }
      input[type=range] { width:80%; margin:10px 0; }
      input[type=number] { width:80px; padding:5px; text-align:center; }
      img.qr { margin:10px auto; border-radius:10px; box-shadow:0 2px 5px rgba(0,0,0,0.1); }
    </style>
  </head>
  <body>
    <h2>Smart Energy Dashboard</h2>

    <div class='card'>
      <h3>🔗 Hubungkan ke WiFi</h3>
      <p><b>SSID:</b> )rawliteral" + String(ssid) + R"rawliteral(<br>
         <b>Password:</b> )rawliteral" + String(password) + R"rawliteral(</p>
      <img class='qr' src=')rawliteral" + qrURL + R"rawliteral(' alt='WiFi QR Code'>
      <p>📱 Scan QR di atas untuk langsung tersambung ke jaringan WiFi</p>
    </div>

    <div class='card'>
      <p><b>Tegangan:</b> <span id='v'>0</span> V</p>
      <canvas id='voltChart'></canvas>
    </div>

    <div class='card'>
      <p><b>Arus:</b> <span id='i'>0</span> A</p>
      <canvas id='currChart'></canvas>
    </div>

    <div class='card'>
      <p><b>Daya:</b> <span id='p'>0</span> W</p>
      <canvas id='powerChart'></canvas>
    </div>

    <div class='card'>
      <p><b>Energi:</b> <span id='e'>0</span> Wh</p>
      <canvas id='energyChart'></canvas>
    </div>

    <div class='card'>
      <p><b>Biaya:</b> Rp <span id='c'>0</span></p>
      <canvas id='costChart'></canvas>
    </div>

    <div class='card'>
      <p><b>Status Relay:</b> <span id='r'>ON ✅</span></p>
      <a href='/on'><button class='on'>NYALAKAN</button></a>
      <a href='/off'><button class='off'>MATIKAN</button></a>
    </div>

    <div class='card'>
      <p><b>Batas Daya Proteksi:</b> <span id='limit'>0</span> W</p>
      <input type='range' id='limitSlider' min='0' max='5000' step='10' value='1800' oninput='updateLimit(this.value)'>
      <input type='number' id='limitBox' value='1800' min='0' step='10' oninput='updateLimit(this.value)'>
      <button onclick='saveLimit()' style='background:#2563eb;color:white;'>Simpan Batas</button>
    </div>

    <script>
      let data = {v:[], i:[], p:[], e:[], c:[]};
      let maxPoints = 20;

      const charts = {};
      const makeChart = (id,color)=>{
        const ctx=document.getElementById(id).getContext('2d');
        charts[id]=new Chart(ctx,{type:'line',data:{labels:[],datasets:[{borderColor:color,borderWidth:2,fill:true,backgroundColor:color+'22',pointRadius:0}]},
        options:{scales:{x:{display:false},y:{display:false}},plugins:{legend:{display:false}},animation:false}});
      };

      makeChart('voltChart','#3b82f6');
      makeChart('currChart','#22c55e');
      makeChart('powerChart','#ef4444');
      makeChart('energyChart','#f59e0b');
      makeChart('costChart','#8b5cf6');

      async function fetchData(){
        const res=await fetch('/data');
        const j=await res.json();
        document.getElementById('v').innerText=j.voltage.toFixed(2);
        document.getElementById('i').innerText=j.current.toFixed(3);
        document.getElementById('p').innerText=j.power.toFixed(2);
        document.getElementById('e').innerText=j.energy.toFixed(3);
        document.getElementById('c').innerText=j.cost.toFixed(2);
        document.getElementById('r').innerText=j.relay ? "ON ✅" : "OFF ⚠";
        document.getElementById('limit').innerText=j.limit.toFixed(0);
        document.getElementById('limitSlider').value=j.limit;
        document.getElementById('limitBox').value=j.limit;

        const m={v:'voltChart',i:'currChart',p:'powerChart',e:'energyChart',c:'costChart'};
        const v={v:j.voltage,i:j.current,p:j.power,e:j.energy,c:j.cost};
        for(let k in m){
          data[k].push(v[k]);
          if(data[k].length>maxPoints)data[k].shift();
          charts[m[k]].data.labels=Array(data[k].length).fill('');
          charts[m[k]].data.datasets[0].data=data[k];
          charts[m[k]].update();
        }
      }

      function updateLimit(val){
        document.getElementById('limitBox').value=val;
        document.getElementById('limitSlider').value=val;
        document.getElementById('limit').innerText=val;
      }

      async function saveLimit(){
        const newLimit=document.getElementById('limitBox').value;
        await fetch('/setlimit?value='+newLimit);
        alert('Batas daya disimpan: '+newLimit+' W');
      }

      setInterval(fetchData,2000);
    </script>
  </body>
  </html>
  )rawliteral";
  return html;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(ADDR_TRIP, TRIP_THRESHOLD);
  if (isnan(TRIP_THRESHOLD) || TRIP_THRESHOLD < 0) TRIP_THRESHOLD = 1800.0;

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke WiFi ");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n✅ WiFi Terhubung!");
  Serial.println(WiFi.localIP());

  server.on("/", [](){ server.send(200, "text/html", getPage()); });
  server.on("/on", [](){ digitalWrite(RELAY_PIN, LOW); relayStatus=true; server.send(200,"text/html",getPage()); });
  server.on("/off", [](){ digitalWrite(RELAY_PIN, HIGH); relayStatus=false; server.send(200,"text/html",getPage()); });

  server.on("/setlimit", [](){
    if(server.hasArg("value")){
      float newLimit = server.arg("value").toFloat();
      if(newLimit >= 0){
        TRIP_THRESHOLD = newLimit;
        EEPROM.put(ADDR_TRIP, TRIP_THRESHOLD);
        EEPROM.commit();
        Serial.printf("✅ Batas daya baru disimpan: %.2f W\n", TRIP_THRESHOLD);
        server.send(200,"text/plain","OK");
      } else server.send(400,"text/plain","Invalid value");
    } else server.send(400,"text/plain","No value");
  });

  server.on("/data", [](){
    cost = energy * 1.5;
    String json = "{";
    json += "\"voltage\":" + String(voltage,2) + ",";
    json += "\"current\":" + String(current,3) + ",";
    json += "\"power\":" + String(power,2) + ",";
    json += "\"energy\":" + String(energy,3) + ",";
    json += "\"cost\":" + String(cost,2) + ",";
    json += "\"limit\":" + String(TRIP_THRESHOLD,0) + ",";
    json += "\"relay\":" + String(relayStatus ? "true" : "false");
    json += "}";
    server.send(200,"application/json",json);
  });

  server.begin();
  Serial.println("🌐 Server siap diakses!");
}

void loop() {
  server.handleClient();

  voltage = pzem.voltage();
  current = pzem.current();
  power = pzem.power();
  energy = pzem.energy();

  avgPower = 0.7 * avgPower + 0.3 * power;
  power = avgPower;

  if (!isnan(power)) {
    if (power > TRIP_THRESHOLD && relayStatus && millis()-lastTripTime>DEBOUNCE_TIME) {
      digitalWrite(RELAY_PIN, HIGH);
      relayStatus = false;
      lastTripTime = millis();
      Serial.printf("⚠ Proteksi aktif! Daya melebihi %.2f W\n", TRIP_THRESHOLD);
    }
  }
  delay(1000);
}
