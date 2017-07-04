#include <ESP8266WiFi.h>
#include <MQTTClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <EEPROM.h>
#include "HX711.h" //

// -For BlueMix connect----
#define ORG "kwxqcy" // имя организации
#define DEVICE_TYPE "SmartCooler" // тип устройства
#define TOKEN "12345678" // - задаешь в IOT хабе

// EEPROM - разделение области памяти
#define ModeoffSet 0 // начальный байт: 0 байт - режим загрузки устройства
#define SizeMode 1 // - размер памяти Mode байт -1
#define SSIDoffSet 1 // начальный байт: SSID
#define SizeSSID 32 // размер памяти SSID байт -32
#define PasswordoffSet 33 //  начальный байт Password
#define SizePassword 32 //  размер памяти   Password
#define DeviceIDoffset 66 // начальный байт Device ID
#define SizeDeviceID 32 // размер памяти Device ID байт
//-----------------------------------------
#define FloatClearWeight 300//  вес площадки float
#define FloatCoolerWeight 304 // вес кулера  float
#define FloatFullBotteWeight 308 //вес полной бутылки float
#define FloatVBottle 312 // объем  бутылки float
#define FloatKoeff 316 // коэффициент float

float ClearWeight;//  вес площадки float
float CoolerWeight; // вес кулера  float
float FullBotteWeight; //вес полной бутылки float
float VBottle; // объем  бутылки float
float Koeff; // коэффициент float

char mqttserver[] = ORG ".messaging.internetofthings.ibmcloud.com"; // подключаемся к Blumix
char topic[] = "iot-2/evt/status/fmt/json";
char restopic[] = "iot-2/cmd/rele/fmt/json";
char authMethod[] = "use-token-auth";
char token[] = TOKEN;
String clientID = "d:" ORG ":" DEVICE_TYPE ":";
char  cID[100]; // используется для формирования конечного Client ID
float dryWeight;
float FullWeight;
//------ Весовая часть------

int DOUT = 5;   // HX711.DOUT Данные с датчика веса
int SCK1 = 4;  // HX711.PD_SCK Тактирование датчика веса
int RELE = 14; // RELE на D05   NodeMCU
int BUTTON = 12; // Кнопка Setup на D06 - NodeMCU
float Vvoter = 0;
float oldVvoter = 500;
HX711 ves; // создаем переменную для работы с АЦП 711
char ssid[32];
char password[32];
char* cfg_ssid = "col01"; // SSID передаваемое в режиме AP кулером
char* cfg_password = "12345678"; // Password для доступа в Cooler

long lastMsg = 0; //
long starttime; // момент перехода в режим 0

String content; // содержимое WEB страницы

String stip;
String stpsw;

String DeviceID;
char   dvid[32];
int statusCode;

int MODE; // Режим загрузки

ESP8266WebServer server(80);
WiFiClientSecure net; // sec
MQTTClient client;
unsigned long lastMillis = 0;

void myconnect(); // <- predefine connect() for setup()
void EEread(char* eeprval, byte offSet, byte Size);
void EEwrite(String indata, byte offSet, byte Size);



void setup() {
	Serial.begin(115200);
	while (!Serial) {
		// wait serial port initialization
	}
	pinMode(BUTTON, INPUT_PULLUP); // Устанавливем пин кнопки на чтение подтягиваем к +

	EEPROM.begin(512);
	MODE = EEPROM.read(0); // определяем режим загрузки   читаем 0 байт EEPROM- если 0 - режим конфигурации если 1 -рабочий режим
	EEread(dvid, DeviceIDoffset, SizeDeviceID); // загружаем значение  Device ID;
	delay(1000);
	if (digitalRead(BUTTON) == LOW)
	{
		MODE = 0;
		EEPROM.write(0, 0);
		EEPROM.commit();
	}

	if (MODE == 1) // Нормальный режим ___________________________________________________________
	{
		Serial.println("Mode-1 - Normal-  Start");
		//Читаем SSID и Password из EEPROM:++++++++++
		EEread(ssid, SSIDoffSet, SizeSSID);
		EEread(password, PasswordoffSet, SizePassword);
		EEPROM.get(FloatClearWeight, ClearWeight);
		EEPROM.get(FloatCoolerWeight, CoolerWeight);
		EEPROM.get(FloatFullBotteWeight, FullBotteWeight);
		EEPROM.get(FloatVBottle, VBottle);
		EEPROM.get(FloatKoeff, Koeff);
		Serial.println("SSID");
		Serial.println(ssid);
		Serial.println("Password");
		Serial.println(password);
		Serial.println("ID client:");
		//Погдотавливаем Client ID и записываем в macив, т.к. String MQTTClient не принимает
		clientID += String(dvid);
		int str_len = clientID.length() + 1;
		clientID.toCharArray(cID, str_len);
		Serial.println(cID);

		//delay(100);
		//WiFi.mode(WIFI_STA);
		//delay(100);
		WiFi.enableAP(0);
		WiFi.enableSTA(1);
		delay(100);
		WiFi.begin(ssid, password);
		//WiFi.begin("Office C2M", "I0t0ff1cE17");
		pinMode(RELE, OUTPUT); // настройка пина RELE на выход
		digitalWrite(RELE, LOW); // включение реле
		ves.begin(DOUT, SCK1, 128); // инициализация АЦП
		client.begin(mqttserver, 8883, net); // 8883-sec 1883 -no sec
		myconnect();
	}

	if (MODE == 0)
	{
		Serial.println("Mode-0 -Config mode- Start");
		WiFi.printDiag(Serial);
		WiFi.enableAP(1);
		WiFi.enableSTA(0);

		ves.begin(DOUT, SCK1, 128);
		IPAddress APIP(10, 0, 0, 100);
		IPAddress gateway(10, 0, 0, 1);
		IPAddress subnet(255, 255, 255, 0);
		WiFi.softAPConfig(APIP, gateway, subnet);
		delay(100);
		WiFi.softAP(cfg_ssid, cfg_password);
		starttime = millis(); // сохраняем время перехода в режим 0, через 10 мин
		launchWebAP(0);//OK
	}
}

void myconnect() {
	Serial.print("checking wifi...");
	while (WiFi.status() != WL_CONNECTED) {
		Serial.print(".");
		delay(1000);
	}
	Serial.print("\nconnecting...");
	while (!client.connect(cID, authMethod, token)) {
		Serial.print("+");
		delay(1000);
	}
	Serial.println("\nconnected!");
	client.subscribe(restopic);
}

// В номрмальном режиме
void messageReceived(String restopic, String payload, char * bytes, unsigned int length) {
	Serial.println("inpuuuuut");
	if (payload == "{\"rel\":1}") {  // Включить реле норамальнозамкнутые контакты
		digitalWrite(RELE, LOW);
		Serial.println("RELE_ON");
	}
	else if (payload == "{\"rel\":0}") { // Выключить реле норамальнозамкнутые контакты
		digitalWrite(RELE, HIGH);
		Serial.println("RELE_OFF");
	}

	else {
		Serial.println("no_action");
	}
}
// В нормальном режиме
String outmessage(float V, char* DeviceID)
{
	String pl = "{ \"d\" : {\"deviceid\":\"";
	pl += DeviceID;
	pl += "\",\"param1\":\"";
	pl += V;
	pl += "\",\"param2\":\"";
	pl += VBottle; //передаем объем бутыли
	pl += "\"}}";
	return pl;
}
void reconnect() {
	// Loop until we're reconnected
	while (!client.connected()) {
		Serial.print("Attempting MQTT connection...");
		// Attempt to connect
		if (client.connect(cID, authMethod, token)) {
			Serial.println("connected");
			// Once connected, publish an announcement...

			String payload = outmessage(Vvoter, dvid);
			client.publish(topic, (char*)payload.c_str());
			// ... and resubscribe
			client.subscribe(restopic);
		}
		else {
			Serial.print("failed, rc=");
			// Serial.print(client.state());
			Serial.println(" try again in 5 seconds");
			// Wait 5 seconds before retrying
			delay(5000);
		}
	}
}
// В нормальном режиме

//_____________________________________________________________

void launchWebAP(int webtype) {
	createWebServer(webtype);
	server.begin();
}
void createWebServer(int webtype)
{
	if (webtype == 0) {
		server.on("/", []() {
			EEread(ssid, SSIDoffSet, SizeSSID);
			EEread(password, PasswordoffSet, SizePassword);
			EEread(dvid, DeviceIDoffset, SizeDeviceID);
			EEPROM.get(FloatClearWeight, ClearWeight);
			EEPROM.get(FloatCoolerWeight, CoolerWeight);
			EEPROM.get(FloatFullBotteWeight, FullBotteWeight);
			EEPROM.get(FloatVBottle, VBottle);
			EEPROM.get(FloatKoeff, Koeff);

			//_______________________________________________________
			content = "<!DOCTYPE HTML>\r\n<html>  <head> <meta http-equiv=\"Content - Type\" content=\"text/html; charset = utf-8\"> </head> <h1>Smart Cooler configuration</h1> <h2>Read EEPROM:</h2>";
			content += "SSID=";
			content += ssid;
			content += "  Password:";
			content += password;
			content += " Device_ID:";
			content += dvid;
			content += " Coefficient:";
			content += Koeff;
			content += " Clear weight:";
			content += ClearWeight;
			content += " Cooler Weight:";
			content += CoolerWeight;
			content += " Full Botte Weight:";
			content += FullBotteWeight;
			content += "Bottle capacity:";
			content += VBottle;

			//_______________________________________________________
			content += " <hr><h1> Input new SSID and  Password </h1>";
			content += "<form method='get' action='setting'>";
			content += "<label>SSID: </label><input name='ip' length=32><br><br>";
			content += "<label>PASSWORD: </label><input name='password' length=32><br><br>";
			content += "<input type='submit' value='save SSID/Password'></form>";
			//______________________________________________________
			content += "<hr><h1>Input new Device_ID </h1> ";
			content += "<form method='get' action='deviceidsetting'>";
			content += "<label>DeviceID: </label><input name='DeviceID' length=32><br><br>";
			content += "<input type='submit' value='Save Device ID'></form>";
			//______________________________________________________
			content += "<h1> Calibrate  </h1>";
			content += "<form method='get' action='dw'><label> Save platform weight- </label><input type='submit' value='Save weight platform'></form>";
			content += "<form method='get' action='colerw'><label> Save dry cooler weight - </label><input type='submit' value='Save dry cooler weight'></form>";
			content += "<form method='get' action='fbottlew'><label> Save full bottle weight - </label>  <input type='submit' value='Save full bottle weight'></form>";
			content += "<form method='get' action='vbottle'><label> Input  bottle capacity and  press save : </label><input type='text' name='vbot' value='0' length=10><input type='submit' value='Save  bottle capacity'></form>";

			//-------------------------------------------------------
			content += "<form method='get' action='koef'>";
			content += "<label>Input calibration weight  and press calibrate: </label><input type='text' name='etalon' value='0' length=10>";
			content += "<input type='submit' value='Calibrate'></form>";
			//______________________________________________________

			content += "<hr><h1>Easy Weight test:</h1> ";
			content += "<form method='get' action='getw'>";
			content += "<input type='submit' value='Easy Weight test'></form>";

			content += "<hr><h1> Switch to work mode </h1>";
			content += "<p><font  color=\"red\"> Reboot device AFTER switching to work mode .</font> </p>";
			content += "<form method='get' action='changemode'><input type='submit' value='Switch to work mode'></form>";
			content += "</html>";
			server.send(200, "text/html", content);
		});

		server.on("/changemode", []() {
			EEPROM.write(0, 1); // устанавливаем Режим нормальной загрузки
			EEPROM.commit();
			content = "<!DOCTYPE HTML>\r\n<html>";
			content = "<p> \"Device switch to work mode, reboot device\"</p></br>}";
			content += "<a href='/'>Return</a>";
			content += "</html>";
			server.send(200, "text/html", content);
			ESP.restart();
		});
		server.on("/setting", []() { // сохраняем SSID и пароль
			String stip = server.arg("ip");
			String stpsw = server.arg("password");
			if (stip.length() > 0) {
				EEwrite(stip, SSIDoffSet, SizeSSID);
				EEwrite(stpsw, PasswordoffSet, SizePassword);
				content = "<!DOCTYPE HTML>\r\n<html>";
				content = "<p>SSID and password save </p>";
				content += "</br>";
				content += "<a href='/'>Return to config</a>";
				content += "</html>";
				server.send(200, "text/html", content);
				Serial.println("Sending 200 -SSID PASWD-OK");
			}
			else {
				content = "{\"Error\":\"404 not found\"}";
				statusCode = 404;
				Serial.println("Sending 404");
			}
			server.send(statusCode, "application/json", content);

		});

		server.on("/dw", []() {     // Вес пустой площадки
			ClearWeight = ves.read_average(30);
			EEPROM.put(FloatClearWeight, ClearWeight);
			EEPROM.commit();
			content = "<!DOCTYPE HTML>\r\n<html>";
			content += "<p>Dry Weight calibrate: </p>";
			content += ClearWeight;
			content += "<a href='/'>Return to config</a>";
			content += "</html>";
			server.send(200, "text/html", content);
		});

		server.on("/colerw", []() { // Вес сухого кулера
			CoolerWeight = ves.read_average(30);
			EEPROM.put(FloatCoolerWeight, CoolerWeight);
			EEPROM.commit();
			content = "<!DOCTYPE HTML>\r\n<html>";
			content += "<p>Cooler Weight calibrate: </p>";
			content += CoolerWeight;
			content += "<a href='/'>Return to config</a>";
			content += "</html>";
			server.send(200, "text/html", content);
		});

		server.on("/fbottlew", []() { // Вес полной бутыли
			FullBotteWeight = ves.read_average(30);
			EEPROM.put(FloatFullBotteWeight, FullBotteWeight);
			EEPROM.commit();
			content = "<!DOCTYPE HTML>\r\n<html>";
			content += "<p>full bottle Weight calibrate: </p>";
			content += FullBotteWeight;
			content += "<a href='/'>Return to config</a>";
			content += "</html>";
			server.send(200, "text/html", content);
		});

		server.on("/vbottle", []() {   // сохраняем объем бутылки в литрах.

			VBottle = server.arg("vbot").toFloat();
			if (VBottle>0)
			{

				EEPROM.put(FloatVBottle, VBottle);
				EEPROM.commit();
				content = "<!DOCTYPE HTML>\r\n<html>";
				content += "<p>Calculate coefficient: <br> </p>";
				content += VBottle;
				content += "<a href='/'>Return to config</a>";
				content += "</html>";
				server.send(200, "text/html", content);
			}
			else
			{
				content = "<!DOCTYPE HTML>\r\n<html>";
				content += "<p>ERROR ! Input  V Bottlet: <br> </p>";
				content += "<a href='/'>Return to config</a>";
				content += "</html>";
				server.send(200, "text/html", content);
			}

		});

		server.on("/koef", []() {   // Вычисляем и сохраняем коэффициент персчета показаний АЦП в КГ.
			float inetalon = server.arg("etalon").toFloat();
			if (inetalon>0)
			{
				Koeff = (ves.read_average(30) - ClearWeight) / inetalon;
				EEPROM.put(FloatKoeff, Koeff);
				EEPROM.commit();
				content = "<!DOCTYPE HTML>\r\n<html>";
				content += "<p>Calculate coefficient: <br> </p>";
				content += Koeff;
				content += "<a href='/'>Return to config</a>";
				content += "</html>";
				server.send(200, "text/html", content);
			}
			else
			{
				content = "<!DOCTYPE HTML>\r\n<html>";
				content += "<p>ERROR ! Input  calibrate weight: <br> </p>";
				content += "<a href='/'>Return to config</a>";
				content += "</html>";
				server.send(200, "text/html", content);
			}

		});

		server.on("/deviceidsetting", []() { // Сохраняем ID устройства
			DeviceID = server.arg("DeviceID");
			EEwrite(DeviceID, DeviceIDoffset, SizeDeviceID);
			content = "<!DOCTYPE HTML>\r\n<html>";
			content += "<p> Deviceid Settings <br>";
			content += "</p>";
			content += "<a href='/'>Return to config</a>";
			content += "</html>";
			server.send(200, "text/html", content);
		});


		server.on("/getw", []() { // Просто весы !
			float myweight = (ves.read_average(30) - ClearWeight) / Koeff;
			content = "<!DOCTYPE HTML>\r\n<html>";
			content += "<p> weight   : ";
			content += myweight;
			content += "</p>";
			content += "<a href='/'>Return to config</a>";
			content += "</html>";
			server.send(200, "text/html", content);

		});




	}
}

void EEread(char* eeprval, byte offSet, byte Size) // Чтение из EEPROM
{
	for (int i = 0; i <= Size; ++i)
	{
		eeprval[i] = char(EEPROM.read(i + offSet));
		if (char(EEPROM.read(i + offSet)) == '\0')
		{
			break;
		}
	}
}

void EEwrite(String indata, byte offSet, byte Size) // Запись в EEPROM
{
	if (indata.length() > 0 && Size >= indata.length())
	{
		for (int i = 0; i <= indata.length(); ++i)
		{
			EEPROM.write(i + offSet, indata[i]);
		}
		EEPROM.commit();
	}
}

//_____________________________________________________________

void loop() {
	if (MODE == 1)
	{
		float bw;
		float cw;
		float noww;

		noww = (ves.read_average(10) - ClearWeight) / Koeff; //определяем текущий вес в кг
		cw = (CoolerWeight - ClearWeight) / Koeff; // вычисляем вес кулера
		bw = (FullBotteWeight - ClearWeight) / Koeff; //вычитаем вес полной бутылки
		Vvoter = noww - cw - bw + VBottle; // и добавляем вес воды = объему дабы скорректировать вес на  вес пустой бутылки

		long now = millis();
		if (Vvoter < oldVvoter - 0.2 || Vvoter > oldVvoter + 1 || now - lastMsg > 120000) // передаем сообщение при изменении массы или по  таймауту 2 мин
		{
			delay(3000); // ждем пока  закочатся колебания вызваные изменением веса

			noww = (ves.read_average(10) - ClearWeight) / Koeff; //определяем текущий вес в кг
			cw = (CoolerWeight - ClearWeight) / Koeff; // вычисляем вес кулера
			bw = (FullBotteWeight - ClearWeight) / Koeff; //вычитаем вес полной бутылки
			Vvoter = noww - cw - bw + VBottle;

			if (!client.connected()) {
				reconnect();
			}
			lastMsg = now;
			oldVvoter = Vvoter;
			String payload = outmessage(Vvoter, dvid);
			Serial.print("Publish message: ");
			Serial.println(payload);
			client.publish(topic, (char*)payload.c_str());
			Serial.println("Ver-170704-1minrel");
			Serial.print("Full_bottle:");
			Serial.println(bw);
			Serial.print("cooler_w:");
			Serial.println(cw);
		}


		client.loop();
		delay(10);
	}
	if (MODE != 1)
	{
		server.handleClient();
		long now = millis();
		if (now - starttime > 900000)  // Через 10 мин нахождения в конфигурационном режиме, перезагружаемся в основной режим
		{
			EEPROM.write(0, 1);
			EEPROM.commit();
			Serial.println("Reboot");
			ESP.restart();
		}
	}
}
