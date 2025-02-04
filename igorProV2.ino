#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ----- WiFi & NTP Settings -----
const char* ssid = "Telecable_okibng";
const char* password = "yfrpuGDR";
// To get Madrid time correctly (e.g., 17:40 instead of 16:40), we force standard time = UTC+2.
  
// ----- OLED Settings -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// D4 is used as the OLED reset (and also the button pin if wired accordingly)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, D4);

// ----- Pin Definitions for Rotary Encoder & Button -----
#define CLK    D6
#define DT     D7
#define SW     D4  // Shared with OLED reset – ensure wiring is correct

// ----- Inactivity Timeout -----
unsigned long lastInputTime = 0;
const unsigned long inactivityTimeout = 60000;  // 60 seconds

// ----- State Machine -----
// Main menu options: FOCUS, STOPWATCH, ANIMATION, STONKS, IDLE  
// FOCUS submenu: START, COUNTDOWN, BACK.
enum State {
  MAIN_MENU,
  FOCUS_MENU,
  FOCUS_START,            // Focus count-up timer (MM:SS)
  FOCUS_COUNTDOWN_SELECT, // Adjust countdown minutes (default 20)
  FOCUS_COUNTDOWN_RUN,    // Countdown timer running (MM:SS with progress bar)
  STOPWATCH_RUN,          // Stopwatch mode (MM:SS)
  ANIMATION_RUN,          // Animation mode (cycles through animations)
  STONKS_RUN,             // STONKS mode (display stock data)
  IDLE_MODE               // Idle mode (full-screen date/time)
};
State currentState = MAIN_MENU;

// ----- Menu Variables -----
String mainMenuOptions[] = {"FOCUS", "STOPWATCH", "ANIMATION", "STONKS", "IDLE"};
const int mainMenuCount = 5;
int mainMenuIndex = 0;

String focusMenuOptions[] = {"START", "COUNTDOWN", "BACK"};
const int focusMenuCount = 3;
int focusMenuIndex = 0;

// ----- Timer Variables -----
// For FOCUS_START (count-up timer) in seconds
unsigned long focusTimerSeconds = 0;

// For FOCUS_COUNTDOWN: selectable minutes (default 20) then countdown (in seconds)
int focusCountdownMinutes = 20;
int countdownRemaining = 0;      // in seconds
int initialCountdownSeconds = 0; // for progress bar

// For STOPWATCH_RUN (in seconds)
unsigned long stopwatchSeconds = 0;

// Common variable for 1-second timing updates:
unsigned long timerPreviousMillis = 0;

// ----- Rotary Encoder & Button Debounce -----
const unsigned long buttonDebounceDelay = 50;
unsigned long lastRotaryTime = 0;
const unsigned long rotaryDebounceDelay = 150;

// ----- Global Variables for ANIMATION_RUN -----
// We'll cycle through three animations.
int currentAnimationIndex = 0;
unsigned long animationSwitchTime = 0;
const int totalAnimations = 3;

// Animation 1: Bouncing ball
int ballX = 0, ballY = 0;
int ballVX = 2, ballVY = 2;

// Animation 2: Random dots (no extra globals needed)

// Animation 3: Falling circles
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
unsigned long stonksLastUpdateTime = 0; // used for auto-cycle (30 seconds)
bool stonksFetched = false;
float stonksCurrentPrice = 0.0;
float stonksPercentChange = 0.0;

// Custom 8x8 bitmap for an up arrow
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

// Custom 8x8 bitmap for a down arrow
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

void handleRotaryInput();
void checkButtonAction();
void handleCounting();
void handleInactivity();


// ----- WiFi & Time Setup -----
// Attempts to connect for up to 10 seconds; if it fails, displays an error and retries.
void setupWiFiAndTime() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected.");
    // Set timezone for Madrid – force standard time = UTC+2
    setenv("TZ", "CET-2CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();
    // Sync time via NTP (offsets 0,0 because TZ is set)
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
    setupWiFiAndTime(); // Retry recursively
  }
}

// ----- Setup Function -----
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Booting Device ===");
  
  // Initialize pins for rotary encoder & button
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);
  pinMode(SW, INPUT_PULLUP);
  
  // Initialize OLED display
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
  // STONKS mode: Auto-cycle every 30 seconds if no input.
  if (currentState == STONKS_RUN) {
    if (!stonksFetched) {
      fetchStonksData();
      stonksLastUpdateTime = millis();
      updateStonksDisplay();
    } else if (millis() - stonksLastUpdateTime > 30000) {
      stonksIndex = (stonksIndex + 1) % numStocks;
      stonksFetched = false;
      stonksLastUpdateTime = millis();
    }
  }
  
  // ANIMATION mode: run the current animation.
  if (currentState == ANIMATION_RUN) {
    runAnimation();
  }
  
  handleRotaryInput();
  checkButtonAction();
  handleCounting();
  handleInactivity();
  
  // In IDLE mode, update full-screen date/time frequently.
  if (currentState == IDLE_MODE) {
    updateDisplayIDLE();
  }
}

// ----- Display Update (for MAIN_MENU, FOCUS, STOPWATCH, etc.) -----
// Header (current date/time) is centered.
void updateDisplay() {
  Serial.print("updateDisplay() called, state: ");
  Serial.println(currentState);
  
  display.clearDisplay();
  if (currentState == IDLE_MODE) return;
  if (currentState == STONKS_RUN) return; // STONKS uses its own display update
  
  // Draw header: current date/time in "HH:MM | DD.MM", centered.
  String header = getCurrentDateTime();
  display.setTextSize(1);
  int headerWidth = header.length() * 6;
  int headerX = (SCREEN_WIDTH - headerWidth) / 2;
  display.setCursor(headerX, 0);
  display.setTextColor(WHITE);
  display.print(header);
  
  // Main text:
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
  }
  
  int textWidth = mainText.length() * 12;
  int x = (SCREEN_WIDTH - textWidth) / 2;
  int y = 20;
  display.setCursor(x, y);
  display.print(mainText);
  
  // For FOCUS_COUNTDOWN_RUN, draw progress bar at bottom.
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
// Displays full date (DD.MM.YYYY) at the top (small, centered) and time (HH:MM) below (large).
void updateDisplayIDLE() {
  display.clearDisplay();
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  
  char dateBuffer[20];
  sprintf(dateBuffer, "%02d.%02d.%04d", timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);
  char timeBuffer[10];
  sprintf(timeBuffer, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
  
  // Draw date in small font, centered.
  display.setTextSize(1);
  int dateWidth = strlen(dateBuffer) * 6;
  int dateX = (SCREEN_WIDTH - dateWidth) / 2;
  display.setCursor(dateX, 0);
  display.setTextColor(WHITE);
  display.print(dateBuffer);
  
  // Draw time in larger font, centered.
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
// Three animations: bouncing ball, random dots, falling circles.

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
  // Auto-cycle animations every 30 seconds.
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
// We now fetch stock data only on entry or when the user scrolls in STONKS mode.
void fetchStonksData() {
  HTTPClient http;
  String url = "https://finnhub.io/api/v1/quote?symbol=";
  url += stockSymbols[stonksIndex];
  url += "&token=";
  url += FINNHUB_API_KEY;
  Serial.print("Fetching stock data from: ");
  Serial.println(url);
  
  WiFiClientSecure client;
  client.setInsecure();  // Disable certificate verification for simplicity
  http.begin(client, url);  // Use secure client for HTTPS
  
  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.print("HTTP response code: ");
    Serial.println(httpCode);
    Serial.print("Payload: ");
    Serial.println(payload);
    
    // Parse current price "c"
    int idxC = payload.indexOf("\"c\":");
    if (idxC != -1) {
      int start = idxC + 4;
      int end = payload.indexOf(",", start);
      String cStr = payload.substring(start, end);
      stonksCurrentPrice = cStr.toFloat();
    }
    // Parse percent change "dp"
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

  // Display current price with "$"
  char priceBuffer[10];
  sprintf(priceBuffer, "%.2f$", stonksCurrentPrice);
  display.setTextSize(2);
  int priceWidth = strlen(priceBuffer) * 12;
  int priceX = (SCREEN_WIDTH - priceWidth) / 2;
  display.setCursor(priceX, 26);
  display.print(priceBuffer);

  // Decide which arrow bitmap to use based on percent change
  const unsigned char* arrowBitmap;
  if (stonksPercentChange >= 0) {
    arrowBitmap = upArrowBitmap;
  } else {
    arrowBitmap = downArrowBitmap;
  }
  // Draw the arrow bitmap at a fixed position (e.g., x = 10, y = 46)
  display.drawBitmap(10, 46, arrowBitmap, 8, 8, WHITE);

  // Display the percent change next to the arrow.
  char changeBuffer[10];
  sprintf(changeBuffer, "%.2f%%", fabs(stonksPercentChange));
  display.setTextSize(2);
  int changeWidth = strlen(changeBuffer) * 12;
  int changeX = 20; // Draw right of the arrow
  display.setCursor(changeX, 46);
  display.print(changeBuffer);

  display.display();
  Serial.print("STONKS display updated for ");
  Serial.println(stockName);
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
    lastInputTime = millis(); // update activity

    // In IDLE, ANIMATION, or STONKS mode, any rotary input exits (in STONKS mode, rotary cycles stock).
    if (currentState == IDLE_MODE) {
      Serial.println("Exiting IDLE mode via rotary input.");
      currentState = MAIN_MENU;
      updateDisplay();
      return;
    }
    if (currentState == ANIMATION_RUN) {
      Serial.println("Exiting ANIMATION mode via rotary input.");
      currentState = MAIN_MENU;
      updateDisplay();
      return;
    }
    if (currentState == STONKS_RUN) {
      // In STONKS mode, rotary input cycles to the next stock.
      stonksIndex = (stonksIndex + rotation + numStocks) % numStocks;
      stonksFetched = false;
      stonksLastUpdateTime = millis();
      fetchStonksData();
      updateStonksDisplay();
      return;
    }
    
    // MAIN_MENU: adjust selection.
    if (currentState == MAIN_MENU) {
      mainMenuIndex = (mainMenuIndex + rotation + mainMenuCount) % mainMenuCount;
      Serial.print("Main menu index: ");
      Serial.println(mainMenuIndex);
      updateDisplay();
    }
    // FOCUS_MENU: adjust submenu selection.
    else if (currentState == FOCUS_MENU) {
      focusMenuIndex = (focusMenuIndex + rotation + focusMenuCount) % focusMenuCount;
      Serial.print("Focus menu index: ");
      Serial.println(focusMenuIndex);
      updateDisplay();
    }
    // FOCUS_COUNTDOWN_SELECT: adjust countdown minutes.
    else if (currentState == FOCUS_COUNTDOWN_SELECT) {
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
      lastInputTime = millis(); // update activity
      
      switch (currentState) {
        case MAIN_MENU:
          if (mainMenuIndex == 0) {  // FOCUS
            currentState = FOCUS_MENU;
            focusMenuIndex = 0;
            Serial.println("Entering FOCUS menu.");
          } else if (mainMenuIndex == 1) {  // STOPWATCH
            currentState = STOPWATCH_RUN;
            stopwatchSeconds = 0;
            timerPreviousMillis = millis();
            Serial.println("Starting STOPWATCH.");
          } else if (mainMenuIndex == 2) {  // ANIMATION
            currentState = ANIMATION_RUN;
            ballX = random(0, SCREEN_WIDTH - 4);
            ballY = random(0, SCREEN_HEIGHT - 4);
            ballVX = 2; ballVY = 2;
            // Reset falling circles:
            for (int i = 0; i < numCircles; i++) {
              circleX[i] = random(0, SCREEN_WIDTH);
              circleY[i] = random(-SCREEN_HEIGHT, 0);
              circleSpeed[i] = random(1, 4);
            }
            animationSwitchTime = millis();
            currentAnimationIndex = 0;
            Serial.println("Entering ANIMATION mode.");
          } else if (mainMenuIndex == 3) {  // STONKS
            currentState = STONKS_RUN;
            stonksIndex = 0;
            stonksFetched = false;
            stonksLastUpdateTime = millis();
            fetchStonksData();
            updateStonksDisplay();
            Serial.println("Entering STONKS mode.");
          } else if (mainMenuIndex == 4) {  // IDLE
            currentState = IDLE_MODE;
            Serial.println("Entering IDLE mode (explicit).");
          }
          updateDisplay();
          break;
          
        case FOCUS_MENU:
          if (focusMenuIndex == 0) {  // START
            currentState = FOCUS_START;
            focusTimerSeconds = 0;
            timerPreviousMillis = millis();
            Serial.println("Starting FOCUS count-up timer.");
          } else if (focusMenuIndex == 1) {  // COUNTDOWN
            currentState = FOCUS_COUNTDOWN_SELECT;
            focusCountdownMinutes = 20;
            Serial.println("Entering FOCUS COUNTDOWN selection.");
          } else if (focusMenuIndex == 2) {  // BACK
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
void handleInactivity() {
  // Only trigger auto-idle if the user is in a menu/submenu:
  if ((currentState == MAIN_MENU || currentState == FOCUS_MENU || currentState == FOCUS_COUNTDOWN_SELECT) &&
      (millis() - lastInputTime > inactivityTimeout)) {
    Serial.println("No input for 60s in a menu; entering IDLE mode.");
    currentState = IDLE_MODE;
    updateDisplay();
  }
}
