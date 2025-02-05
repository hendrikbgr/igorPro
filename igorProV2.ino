#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <base64.h>  // Ensure you have a Base64 library available
#include <ArduinoJson.h> // Install ArduinoJson v6 or later

// ----- WiFi & NTP Settings -----
const char* ssid = "Telecable_okibng";
const char* password = "yfrpuGDR";
// To get Madrid time correctly, we use the proper TZ string for Madrid.

// ----- OLED Settings -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, D4);

// ----- Pin Definitions for Rotary Encoder & Button -----
#define CLK    D6
#define DT     D7
#define SW     D4  // Shared with OLED reset – ensure wiring is correct

// ----- Inactivity Timeout -----
unsigned long lastInputTime = 0;
const unsigned long inactivityTimeout = 60000;  // 60 seconds

// ----- Spotify Credentials & Token Data -----
// (Replace these with your actual Spotify app values.)
const char* SPOTIFY_CLIENT_ID = "892d24efb7be46eda64b117e84746469";
const char* SPOTIFY_CLIENT_SECRET = "efc676374890412bb6dc7ae163f41539";
String spotifyRefreshToken = "AQDn5CQ1ZSgR0vr03yCfr1zoCUJZaMeHHJKPJchF-b_q-OkUYEAtSdDOgTWcXVqlFk3OtW3NtpxZDiVIM8eHN0bs2M6WoPjn4WGRTypG89HcTEhdnxgpzMZg3269QZD3Ors";
String spotifyAccessToken = "BQBu-O3IIOdIYLSIWNcAC-S4bepd7Ad7WSlMzSB7S9pjRlZfB0KMamXb27xUMogJZvhSNof5Zgjx5dEVf8QoSoHqomRTyPdijW9UMYK3d3JtbXR33mYBW5YkXqtqPqAPmSTgTzBY96Dg87MY2gJfIhi74K4jR3CayGUohHaJXGG309vUpcNvB0AFhHTA7VusE2xlRGcWqdVqKkXPqq-5UV33moBt";
unsigned long spotifyTokenExpiry = 0;  // timestamp (millis) when token expires

// For controlling how often we fetch Spotify data:
unsigned long spotifyLastFetchTime = 0;  // fetch every 5 seconds

// ----- New Global Variables for Spotify Volume Control -----
int pendingVolumeDelta = 0;  
unsigned long lastVolumeUpdateTime = 0;
const unsigned long volumeUpdateInterval = 200;  // 200ms delay

// ----- New Global Variables for Spotify Scrolling -----
// These variables control horizontal scrolling for artist and title.
int artistScrollX = 0;
int titleScrollX = 0;
unsigned long lastArtistScrollTime = 0;
unsigned long lastTitleScrollTime = 0;
const unsigned long scrollDelay = 150; // delay (ms) between scroll steps

// ----- State Machine -----
// Main menu options: FOCUS, STOPWATCH, ANIMATION, STONKS, IDLE, SPOTIFY  
enum State {
  MAIN_MENU,
  FOCUS_MENU,
  FOCUS_START,            // Focus count-up timer (MM:SS)
  FOCUS_COUNTDOWN_SELECT, // Adjust countdown minutes (default 20)
  FOCUS_COUNTDOWN_RUN,    // Countdown timer running (MM:SS with progress bar)
  STOPWATCH_RUN,          // Stopwatch mode (MM:SS)
  STOPWATCH_PAUSED,       // Stopwatch paused state
  ANIMATION_RUN,          // Animation mode (cycles through animations)
  STONKS_RUN,             // STONKS mode (display stock data)
  SPOTIFY_RUN,            // Spotify mode (display current track & control playback)
  IDLE_MODE               // Idle mode (full-screen date/time)
};
State currentState = MAIN_MENU;

// ----- Menu Variables -----
String mainMenuOptions[] = {"FOCUS", "STOPWATCH", "ANIMATION", "STONKS", "IDLE", "SPOTIFY"};
const int mainMenuCount = 6;
int mainMenuIndex = 0;

String focusMenuOptions[] = {"START", "COUNTDOWN", "BACK"};
const int focusMenuCount = 3;
int focusMenuIndex = 0;

// ----- Timer Variables -----
unsigned long focusTimerSeconds = 0;
int focusCountdownMinutes = 20;
int countdownRemaining = 0;      // in seconds
int initialCountdownSeconds = 0; // for progress bar
unsigned long stopwatchSeconds = 0;
unsigned long timerPreviousMillis = 0;

// ----- Rotary Encoder & Button Debounce -----
const unsigned long buttonDebounceDelay = 50;
unsigned long lastRotaryTime = 0;
const unsigned long rotaryDebounceDelay = 150;

// ----- Global Variables for ANIMATION_RUN -----
int currentAnimationIndex = 0;
unsigned long animationSwitchTime = 0;
const int totalAnimations = 3;
int ballX = 0, ballY = 0;
int ballVX = 2, ballVY = 2;
const int numCircles = 5;
int circleX[numCircles];
int circleY[numCircles];
int circleSpeed[numCircles];

// ----- Global Variables for STONKS_RUN -----
const char* FINNHUB_API_KEY = "cuh48dpr01qva71stan0cuh48dpr01qva71stang";
const char* stockSymbols[] = {"NVDA", "AAPL", "MSFT"};
const char* stockNames[]   = {"NVIDIA", "APPLE", "MICROSOFT"};
const int numStocks = 3;
int stonksIndex = 0;
unsigned long stonksLastUpdateTime = 0; // auto-cycle (30 seconds)
bool stonksFetched = false;
float stonksCurrentPrice = 0.0;
float stonksPercentChange = 0.0;

// ----- Global Variables for SPOTIFY_RUN -----
String spotifyTrackName = "";
String spotifyArtist = "";
bool spotifyIsPlaying = false;
int spotifyVolume = 50;  // volume percent
unsigned long lastSpotifyButtonTime = 0;
int spotifyClickCount = 0;
const unsigned long spotifyDoubleClickTime = 500; // ms
const unsigned long spotifyLongPressTime = 1000;    // ms

// ----- Custom Bitmaps for Arrows (used in STONKS mode) -----
const unsigned char upArrowBitmap[] PROGMEM = {
  0b00011000,
  0b00111100,
  0b01111110,
  0b11011011,
  0b00011000,
  0b00011000,
  0b00011000,
  0b00011000
};
const unsigned char downArrowBitmap[] PROGMEM = {
  0b00011000,
  0b00011000,
  0b00011000,
  0b00011000,
  0b11011011,
  0b01111110,
  0b00111100,
  0b00011000
};

// ----- Forward Declarations -----
void setupWiFiAndTime();
void updateDisplay();
void updateDisplayIDLE();
String formatTime(int totalSeconds);
String getCurrentDateTime();
void runAnimation();
void animationBouncingBall();
void animationRandomDots();
void animationFallingCircles();

void fetchStonksData();
void updateStonksDisplay();

void refreshSpotifyToken();
void fetchSpotifyData();
void updateSpotifyDisplay();
void adjustSpotifyVolume(int delta);
void toggleSpotifyPlayback();
void skipSpotifyTrack();
void processSpotifyClick();

void handleRotaryInput();
void checkButtonAction();
void handleCounting();
void handleInactivity();
void updateSpotifyVolumeIfNeeded();

// ----- Spotify Token Refresh Function -----
void refreshSpotifyToken() {
  Serial.println("Refreshing Spotify access token...");
  HTTPClient http;
  String url = "https://accounts.spotify.com/api/token";
  
  WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String postData = "grant_type=refresh_token&refresh_token=" + spotifyRefreshToken;
  String credentials = String(SPOTIFY_CLIENT_ID) + ":" + String(SPOTIFY_CLIENT_SECRET);
  String encodedCredentials = base64::encode(credentials);
  http.addHeader("Authorization", "Basic " + encodedCredentials);
  
  int httpCode = http.POST(postData);
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.print("Refresh payload: ");
    Serial.println(payload);
    int idxToken = payload.indexOf("\"access_token\":\"");
    if (idxToken != -1) {
      int start = idxToken + 16;
      int end = payload.indexOf("\"", start);
      spotifyAccessToken = payload.substring(start, end);
      Serial.print("New Spotify Access Token: ");
      Serial.println(spotifyAccessToken);
    }
    int idxExpires = payload.indexOf("\"expires_in\":");
    if (idxExpires != -1) {
      int start = idxExpires + 13;
      int end = payload.indexOf(",", start);
      if (end == -1) end = payload.indexOf("}", start);
      String expiresStr = payload.substring(start, end);
      int expiresIn = expiresStr.toInt();
      spotifyTokenExpiry = millis() + expiresIn * 1000;
      Serial.print("Token expires in: ");
      Serial.print(expiresIn);
      Serial.println(" seconds");
    }
  } else {
    Serial.print("Spotify token refresh failed: ");
    Serial.println(http.errorToString(httpCode));
  }
  http.end();
}

// ----- WiFi & Time Setup -----
void setupWiFiAndTime() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print("...");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected.");
    // Madrid: standard time UTC+1, DST UTC+2.
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();
    configTime(0, 0, "pool.ntp.org");
    Serial.println("Waiting for NTP time sync...");
    time_t now = time(nullptr);
    while (now < 100000) {
      delay(500);
      now = time(nullptr);
      Serial.print("#");
    }
    Serial.println("\nTime synchronized.");
  } else {
    Serial.println("\nWiFi connection failed. Displaying error...");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 20);
    display.print("WiFi Failed!");
    display.setTextSize(1);
    display.setCursor(0, 50);
    display.print("Retrying in 10s...");
    display.display();
    delay(10000);
    setupWiFiAndTime();
  }
}

// ----- Setup Function -----
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Booting Device ===");
  
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);
  pinMode(SW, INPUT_PULLUP);
  
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    for(;;);
  }
  display.clearDisplay();
  display.display();
  Serial.println("OLED initialized.");
  
  setupWiFiAndTime();
  
  lastInputTime = millis();
  timerPreviousMillis = millis();
  animationSwitchTime = millis();
  
  // Initialize falling circles for animation:
  for (int i = 0; i < numCircles; i++) {
    circleX[i] = random(0, SCREEN_WIDTH);
    circleY[i] = random(-SCREEN_HEIGHT, 0);
    circleSpeed[i] = random(1, 4);
  }
  
  updateDisplay();
  Serial.println("Setup complete. Entering main loop.");
}

// ----- Main Loop -----
void loop() {
  // STONKS mode: cycle through stocks every 30 seconds.
  if (currentState == STONKS_RUN) {
    if (millis() - stonksLastUpdateTime > 30000) {
      stonksIndex = (stonksIndex + 1) % numStocks;
      stonksLastUpdateTime = millis();
      stonksFetched = false;
      fetchStonksData();
      updateStonksDisplay();
    }
  }
  
  // ANIMATION mode:
  if (currentState == ANIMATION_RUN) {
    runAnimation();
  }
  
  // SPOTIFY mode:
  if (currentState == SPOTIFY_RUN) {
    if (millis() - spotifyLastFetchTime > 5000) {
      if (millis() > spotifyTokenExpiry - 30000) {
        refreshSpotifyToken();
      }
      fetchSpotifyData();
      spotifyLastFetchTime = millis();
    }
    updateSpotifyVolumeIfNeeded();
    updateSpotifyDisplay();
  }
  
  handleRotaryInput();
  checkButtonAction();
  handleCounting();
  // Do not trigger auto-idle when in STONKS or SPOTIFY mode.
  if (currentState != STONKS_RUN && currentState != SPOTIFY_RUN)
    handleInactivity();
  
  if (currentState == IDLE_MODE) {
    updateDisplayIDLE();
  }
  
  // In SPOTIFY_RUN, process click timing for single vs. double click:
  if (currentState == SPOTIFY_RUN) {
    if (spotifyClickCount > 0 && (millis() - lastSpotifyButtonTime > spotifyDoubleClickTime)) {
      processSpotifyClick();
    }
  }
}

// ----- Display Update for Menus & Timer Modes -----
void updateDisplay() {
  Serial.print("updateDisplay() called, state: ");
  Serial.println(currentState);
  
  display.clearDisplay();
  if (currentState == IDLE_MODE) return;
  if (currentState == STONKS_RUN) return;
  if (currentState == SPOTIFY_RUN) return;
  
  String header = getCurrentDateTime();
  display.setTextSize(1);
  int headerWidth = header.length() * 6;
  int headerX = (SCREEN_WIDTH - headerWidth) / 2;
  display.setCursor(headerX, 0);
  display.setTextColor(WHITE);
  display.print(header);
  
  display.setTextSize(2);
  String mainText = "";
  if (currentState == MAIN_MENU) {
    mainText = mainMenuOptions[mainMenuIndex];
  } else if (currentState == FOCUS_MENU) {
    mainText = focusMenuOptions[focusMenuIndex];
  } else if (currentState == FOCUS_START) {
    mainText = formatTime(focusTimerSeconds);
  } else if (currentState == FOCUS_COUNTDOWN_SELECT) {
    mainText = String(focusCountdownMinutes) + "m";
  } else if (currentState == FOCUS_COUNTDOWN_RUN) {
    mainText = formatTime(countdownRemaining);
  } else if (currentState == STOPWATCH_RUN) {
    mainText = formatTime(stopwatchSeconds);
  } else if (currentState == STOPWATCH_PAUSED) {
    mainText = formatTime(stopwatchSeconds) + " PAUSED";
  }
  
  int textWidth = mainText.length() * 12;
  int x = (SCREEN_WIDTH - textWidth) / 2;
  int y = 20;
  display.setCursor(x, y);
  display.print(mainText);
  
  if (currentState == FOCUS_COUNTDOWN_RUN && initialCountdownSeconds > 0) {
    float progress = float(initialCountdownSeconds - countdownRemaining) / initialCountdownSeconds;
    int barWidth = progress * SCREEN_WIDTH;
    display.drawRect(0, 56, SCREEN_WIDTH, 6, WHITE);
    display.fillRect(0, 56, barWidth, 6, WHITE);
  }
  
  display.display();
  Serial.println("Display updated.");
}

// ----- Idle Mode Display -----
void updateDisplayIDLE() {
  display.clearDisplay();
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  
  char dateBuffer[20];
  sprintf(dateBuffer, "%02d.%02d.%04d", timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);
  char timeBuffer[10];
  sprintf(timeBuffer, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
  
  display.setTextSize(1);
  int dateWidth = strlen(dateBuffer) * 6;
  int dateX = (SCREEN_WIDTH - dateWidth) / 2;
  display.setCursor(dateX, 0);
  display.setTextColor(WHITE);
  display.print(dateBuffer);
  
  display.setTextSize(3);
  int timeWidth = strlen(timeBuffer) * 18;
  int timeX = (SCREEN_WIDTH - timeWidth) / 2;
  display.setCursor(timeX, 20);
  display.print(timeBuffer);
  
  display.display();
  Serial.print("IDLE display updated: ");
  Serial.print(dateBuffer);
  Serial.print(" ");
  Serial.println(timeBuffer);
}

// ----- Utility: Format seconds into MM:SS -----
String formatTime(int totalSeconds) {
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;
  char buf[6];
  sprintf(buf, "%02d:%02d", minutes, seconds);
  return String(buf);
}

// ----- Utility: Get current date/time as "HH:MM | DD.MM" -----
String getCurrentDateTime() {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  char buf[20];
  sprintf(buf, "%02d:%02d | %02d.%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_mday, timeinfo->tm_mon + 1);
  return String(buf);
}

// ----- ANIMATION Functions -----
void animationBouncingBall() {
  ballX += ballVX;
  ballY += ballVY;
  if (ballX <= 0 || ballX >= SCREEN_WIDTH - 4) ballVX = -ballVX;
  if (ballY <= 0 || ballY >= SCREEN_HEIGHT - 4) ballVY = -ballVY;
  display.clearDisplay();
  display.fillCircle(ballX, ballY, 4, WHITE);
  display.display();
  delay(50);
}

void animationRandomDots() {
  display.clearDisplay();
  for (int i = 0; i < 20; i++) {
    int x = random(0, SCREEN_WIDTH);
    int y = random(0, SCREEN_HEIGHT);
    display.drawPixel(x, y, WHITE);
  }
  display.display();
  delay(100);
}

void animationFallingCircles() {
  display.clearDisplay();
  for (int i = 0; i < numCircles; i++) {
    circleY[i] += circleSpeed[i];
    if (circleY[i] > SCREEN_HEIGHT) {
      circleY[i] = 0;
      circleX[i] = random(0, SCREEN_WIDTH);
      circleSpeed[i] = random(1, 4);
    }
    display.fillCircle(circleX[i], circleY[i], 3, WHITE);
  }
  display.display();
  delay(70);
}

void runAnimation() {
  if (millis() - animationSwitchTime > 30000) {
    currentAnimationIndex = (currentAnimationIndex + 1) % totalAnimations;
    animationSwitchTime = millis();
    Serial.print("Switching to animation index: ");
    Serial.println(currentAnimationIndex);
  }
  switch (currentAnimationIndex) {
    case 0:
      animationBouncingBall();
      break;
    case 1:
      animationRandomDots();
      break;
    case 2:
      animationFallingCircles();
      break;
    default:
      break;
  }
}

// ----- STONKS Functions -----
void fetchStonksData() {
  HTTPClient http;
  String url = "https://finnhub.io/api/v1/quote?symbol=";
  url += stockSymbols[stonksIndex];
  url += "&token=";
  url += FINNHUB_API_KEY;
  Serial.print("Fetching stock data from: ");
  Serial.println(url);
  
  WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, url);
  
  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.print("HTTP response code: ");
    Serial.println(httpCode);
    Serial.print("Payload: ");
    Serial.println(payload);
    
    int idxC = payload.indexOf("\"c\":");
    if (idxC != -1) {
      int start = idxC + 4;
      int end = payload.indexOf(",", start);
      String cStr = payload.substring(start, end);
      stonksCurrentPrice = cStr.toFloat();
    }
    int idxDP = payload.indexOf("\"dp\":");
    if (idxDP != -1) {
      int start = idxDP + 5;
      int end = payload.indexOf(",", start);
      if (end == -1) end = payload.indexOf("}", start);
      String dpStr = payload.substring(start, end);
      stonksPercentChange = dpStr.toFloat();
    }
    stonksFetched = true;
    Serial.print("Fetched: Price = ");
    Serial.print(stonksCurrentPrice);
    Serial.print(", Change% = ");
    Serial.println(stonksPercentChange);
  } else {
    Serial.print("HTTP GET failed: ");
    Serial.println(http.errorToString(httpCode));
    stonksFetched = false;
  }
  http.end();
}

void updateStonksDisplay() {
  display.clearDisplay();
  String stockName = stockNames[stonksIndex];
  display.setTextSize(2);
  int nameWidth = stockName.length() * 12;
  int nameX = (SCREEN_WIDTH - nameWidth) / 2;
  display.setCursor(nameX, 0);
  display.setTextColor(WHITE);
  display.print(stockName);
  
  char priceBuffer[10];
  sprintf(priceBuffer, "%.2f$", stonksCurrentPrice);
  display.setTextSize(2);
  int priceWidth = strlen(priceBuffer) * 12;
  int priceX = (SCREEN_WIDTH - priceWidth) / 2;
  display.setCursor(priceX, 26);
  display.print(priceBuffer);
  
  const unsigned char* arrowBitmap = (stonksPercentChange >= 0) ? upArrowBitmap : downArrowBitmap;
  display.drawBitmap(10, 46, arrowBitmap, 8, 8, WHITE);
  
  char changeBuffer[10];
  sprintf(changeBuffer, "%.2f%%", fabs(stonksPercentChange));
  display.setTextSize(2);
  int changeWidth = strlen(changeBuffer) * 12;
  int changeX = 20;
  display.setCursor(changeX, 46);
  display.print(changeBuffer);
  
  display.display();
  Serial.print("STONKS display updated for ");
  Serial.println(stockName);
}

// ----- SPOTIFY Functions -----
// Now uses ArduinoJson to parse the "item" object.
// Additionally, if the artist name is longer than 10 characters or track title longer than 8,
// the text will scroll horizontally.
void fetchSpotifyData() {
  if (millis() > spotifyTokenExpiry - 30000) {
    refreshSpotifyToken();
  }
  
  HTTPClient http;
  String url = "https://api.spotify.com/v1/me/player/currently-playing";
  Serial.print("Fetching Spotify data from: ");
  Serial.println(url);
  
  WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + spotifyAccessToken);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.print("Spotify HTTP code: ");
    Serial.println(httpCode);
    Serial.print("Payload length: ");
    Serial.println(payload.length());
    if (payload.length() == 0) {
      Serial.println("Payload is empty. Possibly no active playback.");
      spotifyTrackName = "";
      spotifyArtist = "";
      spotifyIsPlaying = false;
    } else {
      StaticJsonDocument<1024> filter;
      filter["item"]["name"] = true;
      filter["item"]["artists"][0]["name"] = true;
      filter["is_playing"] = true;
      
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        spotifyTrackName = "";
        spotifyArtist = "";
      } else {
        JsonObject item = doc["item"];
        if (!item.isNull()) {
          spotifyTrackName = item["name"].as<String>();
          JsonArray artists = item["artists"];
          if (!artists.isNull() && artists.size() > 0) {
            spotifyArtist = artists[0]["name"].as<String>();
          } else {
            spotifyArtist = "";
          }
        } else {
          spotifyTrackName = "";
          spotifyArtist = "";
        }
        spotifyIsPlaying = doc["is_playing"].as<bool>();
      }
    }
  } else {
    Serial.print("Spotify GET failed: ");
    Serial.println(http.errorToString(httpCode));
  }
  http.end();
}

void updateSpotifyDisplay() {
  display.clearDisplay();
  // Animate artist if longer than 10 characters; otherwise, center it.
  display.setTextSize(1);
  String artistText = spotifyArtist;
  if (artistText.length() == 0)
    artistText = "No Artist";
  
  int artistDisplayWidth = artistText.length() * 6;
  // If the artist text is wider than SCREEN_WIDTH and longer than 10 characters, scroll it.
  if (artistText.length() > 10 && artistDisplayWidth > SCREEN_WIDTH) {
    // Update the scroll offset
    if (millis() - lastArtistScrollTime > scrollDelay) {
      artistScrollX--;
      lastArtistScrollTime = millis();
    }
    // Reset when fully scrolled out
    if (artistScrollX < -artistDisplayWidth)
      artistScrollX = SCREEN_WIDTH;
  } else {
    // Center if not animating
    artistScrollX = (SCREEN_WIDTH - artistDisplayWidth) / 2;
  }
  display.setCursor(artistScrollX, 0);
  display.setTextColor(WHITE);
  display.print(artistText);
  
  // Animate track title if longer than 8 characters; otherwise, center it.
  display.setTextSize(2);
  String titleText = spotifyTrackName;
  if (titleText.length() == 0)
    titleText = "No Track";
  
  int titleDisplayWidth = titleText.length() * 12;
  if (titleText.length() > 8 && titleDisplayWidth > SCREEN_WIDTH) {
    if (millis() - lastTitleScrollTime > scrollDelay) {
      titleScrollX--;
      lastTitleScrollTime = millis();
    }
    if (titleScrollX < -titleDisplayWidth)
      titleScrollX = SCREEN_WIDTH;
  } else {
    titleScrollX = (SCREEN_WIDTH - titleDisplayWidth) / 2;
  }
  display.setCursor(titleScrollX, 16);
  display.print(titleText);
  
  display.display();
}

void adjustSpotifyVolume(int delta) {
  pendingVolumeDelta += delta;
}

void updateSpotifyVolumeIfNeeded() {
  if (millis() - lastVolumeUpdateTime > volumeUpdateInterval && pendingVolumeDelta != 0) {
    spotifyVolume += pendingVolumeDelta;
    if (spotifyVolume < 0) spotifyVolume = 0;
    if (spotifyVolume > 100) spotifyVolume = 100;
    
    HTTPClient http;
    String url = String("https://api.spotify.com/v1/me/player/volume?volume_percent=") + String(spotifyVolume);
    WiFiClientSecure client;
    client.setInsecure();
    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + spotifyAccessToken);
    int httpCode = http.PUT("");
    if (httpCode == 204) {
      Serial.print("Spotify volume set to ");
      Serial.println(spotifyVolume);
    } else {
      Serial.print("Failed to set Spotify volume: ");
      Serial.println(http.errorToString(httpCode));
    }
    http.end();
    pendingVolumeDelta = 0;
    lastVolumeUpdateTime = millis();
  }
}

void toggleSpotifyPlayback() {
  HTTPClient http;
  String url;
  if (spotifyIsPlaying) {
    url = "https://api.spotify.com/v1/me/player/pause";
  } else {
    url = "https://api.spotify.com/v1/me/player/play";
  }
  WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + spotifyAccessToken);
  int httpCode = http.PUT("");
  if (httpCode == 204) {
    Serial.println("Spotify playback toggled.");
    spotifyIsPlaying = !spotifyIsPlaying;
  } else {
    Serial.print("Failed to toggle Spotify playback: ");
    Serial.println(http.errorToString(httpCode));
  }
  http.end();
}

void skipSpotifyTrack() {
  HTTPClient http;
  String url = "https://api.spotify.com/v1/me/player/next";
  WiFiClientSecure client;
  client.setInsecure();
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + spotifyAccessToken);
  int httpCode = http.POST("");
  if (httpCode == 204) {
    Serial.println("Skipped to next Spotify track.");
  } else {
    Serial.print("Failed to skip Spotify track: ");
    Serial.println(http.errorToString(httpCode));
  }
  http.end();
}

void processSpotifyClick() {
  if (spotifyClickCount >= 2) {
    Serial.println("Spotify: Double click detected, skipping track.");
    skipSpotifyTrack();
  } else {
    Serial.println("Spotify: Single click detected, toggling play/pause.");
    toggleSpotifyPlayback();
  }
  spotifyClickCount = 0;
  fetchSpotifyData();
  updateSpotifyDisplay();
}

// ----- Handle Rotary Encoder Input -----
int getRotation() {
  static int lastCLK = digitalRead(CLK);
  int currentCLK = digitalRead(CLK);
  if (currentCLK == LOW && lastCLK == HIGH && (millis() - lastRotaryTime > rotaryDebounceDelay)) {
    lastRotaryTime = millis();
    int dtVal = digitalRead(DT);
    lastCLK = currentCLK;
    return (dtVal != currentCLK) ? 1 : -1;
  }
  lastCLK = currentCLK;
  return 0;
}

void handleRotaryInput() {
  int rotation = getRotation();
  if (rotation != 0) {
    Serial.print("Rotary input: ");
    Serial.println(rotation);
    lastInputTime = millis();
    
    // In IDLE or ANIMATION mode, exit to MAIN_MENU.
    if (currentState == IDLE_MODE || currentState == ANIMATION_RUN) {
      Serial.println("Exiting current mode via rotary input.");
      currentState = MAIN_MENU;
      updateDisplay();
      return;
    }
    // In SPOTIFY mode, adjust volume.
    if (currentState == SPOTIFY_RUN) {
      adjustSpotifyVolume(rotation * 5);
      updateSpotifyVolumeIfNeeded();
      fetchSpotifyData();
      updateSpotifyDisplay();
      return;
    }
    if (currentState == STONKS_RUN) {
      stonksIndex = (stonksIndex + rotation + numStocks) % numStocks;
      stonksFetched = false;
      stonksLastUpdateTime = millis();
      fetchStonksData();
      updateStonksDisplay();
      return;
    }
    
    if (currentState == MAIN_MENU) {
      mainMenuIndex = (mainMenuIndex + rotation + mainMenuCount) % mainMenuCount;
      Serial.print("Main menu index: ");
      Serial.println(mainMenuIndex);
      updateDisplay();
    } else if (currentState == FOCUS_MENU) {
      focusMenuIndex = (focusMenuIndex + rotation + focusMenuCount) % focusMenuCount;
      Serial.print("Focus menu index: ");
      Serial.println(focusMenuIndex);
      updateDisplay();
    } else if (currentState == FOCUS_COUNTDOWN_SELECT) {
      focusCountdownMinutes = max(1, focusCountdownMinutes + rotation);
      Serial.print("Focus countdown minutes: ");
      Serial.println(focusCountdownMinutes);
      updateDisplay();
    }
  }
}

// ----- Check Button Action -----
void checkButtonAction() {
  if (digitalRead(SW) == LOW) {
    delay(buttonDebounceDelay);
    if (digitalRead(SW) == LOW) {
      Serial.println("Button pressed.");
      lastInputTime = millis();
      
      switch (currentState) {
        case MAIN_MENU:
          if (mainMenuIndex == 0) {
            currentState = FOCUS_MENU;
            focusMenuIndex = 0;
            Serial.println("Entering FOCUS menu.");
          } else if (mainMenuIndex == 1) {
            currentState = STOPWATCH_RUN;
            stopwatchSeconds = 0;
            timerPreviousMillis = millis();
            Serial.println("Starting STOPWATCH.");
          } else if (mainMenuIndex == 2) {
            currentState = ANIMATION_RUN;
            ballX = random(0, SCREEN_WIDTH - 4);
            ballY = random(0, SCREEN_HEIGHT - 4);
            ballVX = 2; ballVY = 2;
            for (int i = 0; i < numCircles; i++) {
              circleX[i] = random(0, SCREEN_WIDTH);
              circleY[i] = random(-SCREEN_HEIGHT, 0);
              circleSpeed[i] = random(1, 4);
            }
            animationSwitchTime = millis();
            currentAnimationIndex = 0;
            Serial.println("Entering ANIMATION mode.");
          } else if (mainMenuIndex == 3) {
            currentState = STONKS_RUN;
            stonksIndex = 0;
            stonksFetched = false;
            stonksLastUpdateTime = millis();
            fetchStonksData();
            updateStonksDisplay();
            Serial.println("Entering STONKS mode.");
          } else if (mainMenuIndex == 4) {
            currentState = IDLE_MODE;
            Serial.println("Entering IDLE mode (explicit).");
          } else if (mainMenuIndex == 5) {
            currentState = SPOTIFY_RUN;
            spotifyLastFetchTime = 0;  // Force immediate fetch
            // Reset scrolling positions for artist and title:
            artistScrollX = 0;
            titleScrollX = 0;
            lastArtistScrollTime = millis();
            lastTitleScrollTime = millis();
            fetchSpotifyData();
            updateSpotifyDisplay();
            Serial.println("Entering SPOTIFY mode.");
          }
          updateDisplay();
          break;
          
        case FOCUS_MENU:
          if (focusMenuIndex == 0) {
            currentState = FOCUS_START;
            focusTimerSeconds = 0;
            timerPreviousMillis = millis();
            Serial.println("Starting FOCUS count-up timer.");
          } else if (focusMenuIndex == 1) {
            currentState = FOCUS_COUNTDOWN_SELECT;
            focusCountdownMinutes = 20;
            Serial.println("Entering FOCUS COUNTDOWN selection.");
          } else if (focusMenuIndex == 2) {
            currentState = MAIN_MENU;
            Serial.println("Returning to MAIN_MENU from FOCUS menu.");
          }
          updateDisplay();
          break;
          
        case FOCUS_START:
          Serial.print("Stopping FOCUS timer at: ");
          Serial.println(formatTime(focusTimerSeconds));
          updateDisplay();
          delay(3000);
          currentState = FOCUS_MENU;
          updateDisplay();
          break;
          
        case FOCUS_COUNTDOWN_SELECT:
          initialCountdownSeconds = focusCountdownMinutes * 60;
          countdownRemaining = initialCountdownSeconds;
          currentState = FOCUS_COUNTDOWN_RUN;
          timerPreviousMillis = millis();
          Serial.print("Starting FOCUS COUNTDOWN for ");
          Serial.print(focusCountdownMinutes);
          Serial.println(" minutes.");
          updateDisplay();
          break;
          
        case FOCUS_COUNTDOWN_RUN:
          Serial.println("Stopping FOCUS COUNTDOWN early.");
          currentState = FOCUS_MENU;
          updateDisplay();
          break;
          
        case STOPWATCH_RUN:
          currentState = STOPWATCH_PAUSED;
          Serial.println("Stopwatch paused.");
          updateDisplay();
          break;
          
        case STOPWATCH_PAUSED:
          Serial.print("Stopwatch stopped at: ");
          Serial.println(formatTime(stopwatchSeconds));
          currentState = MAIN_MENU;
          updateDisplay();
          break;
          
        case STONKS_RUN:
          Serial.println("Exiting STONKS mode via button.");
          currentState = MAIN_MENU;
          updateDisplay();
          break;
          
        case IDLE_MODE:
          Serial.println("Exiting IDLE mode via button.");
          currentState = MAIN_MENU;
          updateDisplay();
          break;
          
        case ANIMATION_RUN:
          Serial.println("Exiting ANIMATION mode via button.");
          currentState = MAIN_MENU;
          updateDisplay();
          break;
          
        case SPOTIFY_RUN:
          { // Handle Spotify button press with timing for single vs. double vs. long press.
            unsigned long pressStart = millis();
            while(digitalRead(SW) == LOW) { delay(10); }
            unsigned long pressDuration = millis() - pressStart;
            if (pressDuration >= spotifyLongPressTime) {
              Serial.println("Spotify: Long press detected, exiting to MAIN_MENU.");
              currentState = MAIN_MENU;
              updateDisplay();
            } else {
              if (millis() - lastSpotifyButtonTime < spotifyDoubleClickTime) {
                spotifyClickCount++;
              } else {
                spotifyClickCount = 1;
              }
              lastSpotifyButtonTime = millis();
            }
          }
          break;
          
        default:
          break;
      }
      
      while (digitalRead(SW) == LOW) { delay(10); }
      delay(50);
    }
  }
}

// ----- Handle Timer Counting -----
void handleCounting() {
  unsigned long now = millis();
  if (now - timerPreviousMillis < 1000) return;
  timerPreviousMillis = now;
  
  if (currentState == FOCUS_START) {
    focusTimerSeconds++;
    Serial.print("FOCUS timer: ");
    Serial.println(formatTime(focusTimerSeconds));
    updateDisplay();
  }
  else if (currentState == FOCUS_COUNTDOWN_RUN) {
    if (countdownRemaining > 0) {
      countdownRemaining--;
      Serial.print("FOCUS COUNTDOWN: ");
      Serial.println(formatTime(countdownRemaining));
      updateDisplay();
    } else {
      Serial.println("FOCUS COUNTDOWN finished.");
      currentState = FOCUS_MENU;
      updateDisplay();
    }
  }
  else if (currentState == STOPWATCH_RUN) {
    stopwatchSeconds++;
    Serial.print("STOPWATCH: ");
    Serial.println(formatTime(stopwatchSeconds));
    updateDisplay();
  }
}

// ----- Handle Inactivity -----
// Auto-idle only if in a menu/submenu (excluding STONKS and SPOTIFY).
void handleInactivity() {
  if ((currentState == MAIN_MENU || currentState == FOCUS_MENU || currentState == FOCUS_COUNTDOWN_SELECT || currentState == STONKS_RUN) &&
      (millis() - lastInputTime > inactivityTimeout)) {
    Serial.println("No input for 60s in a menu; entering IDLE mode.");
    currentState = IDLE_MODE;
    updateDisplay();
  }
}

