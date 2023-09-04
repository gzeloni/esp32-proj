// Include Wifi library
#include <WiFi.h>
// Include time library
#include <time.h>
// Include Firebase for ESP32 library
#include <FirebaseESP32.h>
// Include ESP Async Web Server for hosting a web interface config
#include <ESPAsyncWebSrv.h>
// Include Preferences for data persistence
#include <Preferences.h>
// Include the NTPClient library
#include <NTPClient.h>
// Include Wifi UDP for Firebase communications
#include <WiFiUdp.h>
// Provide the token generation process info.
#include <addons/TokenHelper.h>
// Provide the RTDB payload printing info and other helper functions.
#include <addons/RTDBHelper.h>

// Firebase host and api-key
#define DATABASE_URL "<DATABASE-URL-HERE>"
#define API_KEY "<API-KEY-HERE>"

// Define the sensor pins (it's soldered)
#define TRIGGER_PIN 2
#define ECHO_PIN 4

// Start a server on port 80 for web interface
AsyncWebServer server(80);

// Define the Wifi AP Name
const char *ssid = "Sensor";
// Define the Wifi AP Password
const char *password = "@sensor_pass";

// Define the required PARAMS
const char *PARAM_WIFI_NAME = "wifiName";
const char *PARAM_WIFI_PASSWORD = "wifiPassword";
const char *PARAM_FIREBASE_USER = "firebaseUser";
const char *PARAM_FIREBASE_PASSWORD = "firebasePass";

// Booleans
bool configCompleted = false;
bool shouldRestart = false;
bool isAuthenticated = false;

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Variable to save USER UID
String uid;
unsigned long sendDataPrevMillis = 0;
unsigned long count = 0;
int readingCount = 0;

// Create a WiFiUDP object to connect to NTP
WiFiUDP ntpUDP;
// Create an instance of NTPClient
NTPClient timeClient(ntpUDP, "pool.ntp.org");

#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
WiFiMulti multi;
#endif

// Function to serve the web interface
void handleRootPage(AsyncWebServerRequest *request)
{
    Preferences preferences;
    preferences.begin("myApp", false);

    String wifiNameValue = preferences.getString("wifiName");
    String firebaseUserValue = preferences.getString("firebaseUser");

    preferences.end();

    String html = "<html><head><meta charset='UTF-8'><style>";
    html += "body { text-align: center; }";
    html += "</style></head><body>";
    html += "<h1>Settings</h1>";
    html += "<form method='post' action='/save'>";
    html += "<label>Wi-Fi Network Name:</label><br>";
    html += "<input type='text' name='wifiName' value='" + wifiNameValue + "'><br>";
    html += "<label>Wi-Fi Network Password:</label><br>";
    html += "<input type='password' name='wifiPassword'><br>";
    html += "<label>Firebase User:</label><br>";
    html += "<input type='text' name='firebaseUser' value='" + firebaseUserValue + "'><br>";
    html += "<label>Firebase Password:</label><br>";
    html += "<input type='password' name='firebasePass'><br>";
    html += "<input type='submit' value='Save'>";
    html += "</form>";
    html += "</body></html>";

    request->send(200, "text/html", html);
}

// Function to save the data from Web Interface
void handleSaveData(AsyncWebServerRequest *request)
{
    if (request->hasArg(PARAM_WIFI_NAME) && request->hasArg(PARAM_WIFI_PASSWORD) && request->hasArg(PARAM_FIREBASE_USER) && request->hasArg(PARAM_FIREBASE_PASSWORD))
    {
        String wifiNameValue = request->arg(PARAM_WIFI_NAME);
        String wifiPasswordValue = request->arg(PARAM_WIFI_PASSWORD);
        String firebaseUserValue = request->arg(PARAM_FIREBASE_USER);
        String firebasePassValue = request->arg(PARAM_FIREBASE_PASSWORD);
        Preferences preferences;
        preferences.begin("myApp", false);

        preferences.putString("wifiName", wifiNameValue);
        preferences.putString("wifiPassword", wifiPasswordValue);
        preferences.putString("firebaseUser", firebaseUserValue);
        preferences.putString("firebasePass", firebasePassValue);

        preferences.end();

        configCompleted = true;

        request->send(200, "text/plain", "Data saved successfully!");
        Serial.println("Data saved successfully!");
        shouldRestart = true;
    }
    else
    {
        request->send(400, "text/plain", "Invalid parameters!");
    }
}

// This function runs the tasks for HCSR04 sensor
void ultrasonic_task(void *parameter)
{
    // Get the MAC address of the device in decimal
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    String macAddress;
    for (int i = 0; i < 6; i++)
    {
        if (mac[i] < 16)
        {
            macAddress += "0";
        }
        macAddress += String(mac[i], HEX);
    }

    pinMode(TRIGGER_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    while (1)
    {
        digitalWrite(TRIGGER_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIGGER_PIN, LOW);
        unsigned long start_time = 0;
        unsigned long end_time = 0;

        while (digitalRead(ECHO_PIN) == LOW)
        {
            start_time = micros();
        }

        while (digitalRead(ECHO_PIN) == HIGH)
        {
            end_time = micros();
        }

        unsigned long pulse_duration = end_time - start_time;
        float distance = pulse_duration * 0.0343 / 2;

        // Get the MAC address of the ESP32
        uint8_t mac[6];
        WiFi.macAddress(mac);
        String macAddress;
        for (int i = 0; i < 6; ++i)
        {
            macAddress += String(mac[i], DEC);
        }

        // Get the current timestamp
        time_t timestamp = timeClient.getEpochTime();
        char timestampString[20];
        sprintf(timestampString, "%lu", timestamp);

        if (strlen(timestampString) >= 8)
        {
            // Format the message
            String data = macAddress + ", " + String(timestampString) + ", " + String(distance);

            // Write the distance to Firebase using the path based on the device MAC and timestamp
            String firebasePath = "/" + uid + "/" + String(readingCount);

            if (Firebase.RTDB.setString(&fbdo, firebasePath.c_str(), data))
            {
                Serial.println("Distance sent to Firebase: " + String(distance));

                Preferences preferences;
                preferences.begin("myApp", false);
                preferences.putInt("timeCount", readingCount);
                preferences.end();

                readingCount++;
            }
            else
            {
                Serial.println("Failed to send distance to Firebase");
                isAuthenticated = false;
            }

            delay(300000);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Starting system modules...");

    Preferences preferences;
    preferences.begin("myApp", false);

    String wifiNameValue = preferences.getString("wifiName");
    String wifiPasswordValue = preferences.getString("wifiPassword");
    String firebaseUserValue = preferences.getString("firebaseUser");
    String firebasePassValue = preferences.getString("firebasePass");
    readingCount = preferences.getInt("timeCount");
    readingCount++;

    preferences.end();

    Serial.println("Starting sensors...");

    if (wifiNameValue.length() > 0 && wifiPasswordValue.length() > 0 && firebaseUserValue.length() > 0 && firebasePassValue.length() > 0)
    {
        configCompleted = true;
        Serial.println("Configuration loaded from flash memory");

        WiFi.begin(wifiNameValue.c_str(), wifiPasswordValue.c_str());

        while (WiFi.status() != WL_CONNECTED)
        {
            delay(1000);
            Serial.print(".");
        }

        Serial.println("");
        Serial.print("Connected to Wi-Fi network: ");
        Serial.println(wifiNameValue);
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());

        // Firebase setup
        config.api_key = API_KEY;
        auth.user.email = firebaseUserValue;
        auth.user.password = firebasePassValue;
        config.database_url = DATABASE_URL;
        config.token_status_callback = tokenStatusCallback;

        Firebase.begin(&config, &auth);

        int waitUid = 0;
        // Getting the user UID might take a few seconds
        Serial.println("Getting User UID");

        while ((auth.token.uid) == "" && waitUid < 10)
        {
            Serial.print('.');
            waitUid++;
            delay(1000);
        }
        if (waitUid > 9)
        {
            isAuthenticated = false;
        }
        else
        {
            // Print user UID
            uid = auth.token.uid.c_str();
            Serial.print("User UID: ");
            Serial.print(uid);
            Firebase.reconnectWiFi(true);
            isAuthenticated = true;
            // Initialize NTPClient
            timeClient.begin();
            // Add a delay here to allow time for Firebase authentication
            delay(5000);

            xTaskCreatePinnedToCore(
                ultrasonic_task,   // function to be executed
                "ultrasonic_task", // name of the task
                20000,             // stack size of the task
                NULL,              // parameter passed to the task (in this case, none)
                1,                 // priority of the task
                NULL,              // task handle
                1                  // core number (0 or 1)
            );
        }
    }
    else
    {
        // Create Access Point (AP)
        WiFi.softAP(ssid, password);
        IPAddress apIP(192, 168, 4, 1);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        Serial.print("Access Point (AP) IP: ");
        Serial.println(WiFi.softAPIP());
    }

    server.on("/", HTTP_GET, handleRootPage);
    server.on("/save", HTTP_POST, handleSaveData);
    server.begin();
}

void loop()
{
    if (shouldRestart)
    {
        delay(1000); // Small delay to allow sending the response
        ESP.restart();
    }

    if (Firebase.isTokenExpired())
    {
        Firebase.refreshToken(&config);
        Serial.println("Refresh token");
    }

    if (isAuthenticated == true)
    {
        WiFi.softAPdisconnect(true);
    }
    else
    {
        // Reactivate the default AP network so the user can correct the access
        WiFi.softAP(ssid, password);
        Serial.println("Connection error or authentication with Firebase. AP network activated.");
    }

    timeClient.update();
    delay(100);
}