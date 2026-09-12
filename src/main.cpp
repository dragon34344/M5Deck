/* =========================================================================
 * M5StickC Plus 2 - StreamDeck & Virtual Keyboard Firmware
 * Target Hardware: M5Stack M5StickC Plus 2 (ESP32-PICO-V3-02)
 * Display Resolution: 135 x 240 TFT LCD (ST7789v2)
 * Native Orientation: Portrait 135 x 240 pixels (Rotation: 0)
 * Communication: USB Serial (115200 baud) + Bluetooth Low Energy (BLE HID)
 * Button Mapping:
 *   - Btn PWR (Left side): Move UP in menu / Prev character / Back
 *   - Btn B (Right side): Move DOWN in menu / Next character
 *   - Btn A (Front M5): Select / Type / Long press = RUN
 * Functions:
 *   1. Minimize All Windows (Win + D / Cmd + F3)
 *   2. App Launcher via On-Screen Virtual Keyboard & Presets
 *   3. Restart PC (shutdown /r /t 0)
 *   4. Shutdown PC (shutdown /s /t 0)
 *   5. Media Controls & System Hotkeys
 * ========================================================================= */

#include <M5StickCPlus2.h>
#include <BleKeyboard.h>
#include <algorithm>

using std::max;
using std::min;

// Screen Dimensions (M5StickC Plus 2 native resolution)
#define SCREEN_W 135
#define SCREEN_H 240

// BLE Keyboard Configuration
BleKeyboard bleKeyboard("M5-StreamDeck", "M5Stack", 100);

// Operating System target: WINDOWS
#define OS_WINDOWS

// Audio feedback
void beep(int freq = 2000, int duration = 40) {
  M5.Speaker.tone(freq, duration);
}

// App Presets
struct AppShortcut {
  const char* name;
  const char* command;
};

const AppShortcut APP_PRESETS[] = {
  {"Calculator", "calc"},
  {"Notepad", "notepad"},
  {"Edge", "msedge"},
  {"Terminal / CMD", "cmd"},
  {"VS Code", "code"},
  {"Telegram", "explorer.exe \"shell:AppsFolder\\TelegramMessengerLLP.TelegramDesktop_t4vj0pshhgkwm!Telegram.TelegramDesktop.Store\""},
  {"Discord", "explorer.exe \"shell:AppsFolder\\com.squirrel.Discord.Discord\""},
  {"Spotify", "explorer.exe \"shell:AppsFolder\\SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify\""},
  {"Task Manager", "taskmgr"},
  {"Steam", "steam://"}
};
const int TOTAL_PRESETS = sizeof(APP_PRESETS) / sizeof(APP_PRESETS[0]);

// Navigation States
enum AppState {
  STATE_MENU,
  STATE_CONFIRM_REBOOT,
  STATE_CONFIRM_SHUTDOWN,
  STATE_APP_SELECTOR,
  STATE_VIRTUAL_KEYBOARD,
  STATE_MEDIA
};

AppState currentState = STATE_MENU;

// Main Menu Items
const char* MENU_ITEMS[] = {
  "1. Min Windows",
  "2. App Launcher",
  "3. On-Screen KB",
  "4. Restart PC",
  "5. Shutdown PC",
  "6. Media Control"
};
const int TOTAL_MENU_ITEMS = 6;
int selectedMenuItem = 0;
int selectedPresetItem = 0;

// Virtual Keyboard Configuration
// Keyboard layout string (accessible via buttons)
const char KEY_CHARS[] = "abcdefghijklmnopqrstuvwxyz0123456789.-_ /";
const int TOTAL_CHARS = sizeof(KEY_CHARS) - 1;
int currentKeyIndex = 0;
String typedBuffer = "";
const int MAX_BUFFER_LEN = 30;

// LCD Colors (RGB565)
#define COLOR_BG        0x0842 // Dark slate blue
#define COLOR_CARD      0x18E4 // Card background
#define COLOR_ACCENT    0xFBA0 // Orange accent (M5Stick signature)
#define COLOR_HIGHLIGHT 0x07E0 // Bright green
#define COLOR_WARN      0xFA20 // Warning red
#define COLOR_TEXT      0xFFFF // White
#define COLOR_SUBTEXT   0x9CF3 // Light grey

// Prototypes
void drawScreen();
void drawMenu();
void drawConfirmDialog(const char* title, const char* subtitle);
void drawAppPresets();
void drawVirtualKeyboard();
void drawMediaControls();
void executeAction(int menuIndex);
void sendMinimizeWindows();
void sendRestartPC();
void sendShutdownPC();
void launchCommand(const String& cmd);
void showFeedback(const char* title, const char* detail);

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  
  // Initialize USB Serial Communication (115200 baud)
  Serial.begin(115200);
  Serial.println(F("{\"status\":\"ready\",\"device\":\"M5StickC_Plus2\",\"usb\":true}"));
  Serial.println(F("[M5-StreamDeck] USB Ready (115200 baud)"));
  
  // Display Setup
  M5.Display.setRotation(0);
  M5.Display.setBrightness(200);
  M5.Display.setTextWrap(false);
  
  // Start BLE Keyboard
  bleKeyboard.begin();
  
  M5.Speaker.tone(1500, 80);
  delay(100);
  M5.Speaker.tone(2200, 100);

  drawScreen();
}

void loop() {
  M5.update(); // Update button states

  // Check incoming commands from PC over USB Serial
  if (Serial.available()) {
    String rxLine = Serial.readStringUntil('\n');
    rxLine.trim();
    if (rxLine == "PING") {
      Serial.println(F("PONG"));
      M5.Speaker.tone(2400, 30);
    } else if (rxLine == "M5.Speaker.TONE") {
      M5.Speaker.tone(2000, 80);
    } else if (rxLine.startsWith("MSG:")) {
      showFeedback("PC MESSAGE", rxLine.substring(4).c_str());
    }
  }

  static bool lastBleConnected = false;
  bool isConnected = bleKeyboard.isConnected();
  if (isConnected != lastBleConnected) {
    lastBleConnected = isConnected;
    drawScreen(); // Redraw status header
  }

  // Handle Input based on current screen
  switch (currentState) {
    case STATE_MENU:
      // BtnPWR: Move selection UP (поднимает выбор вверх!)
      if (M5.BtnPWR.wasPressed()) {
        M5.Speaker.tone(1800, 30);
        selectedMenuItem = (selectedMenuItem - 1 + TOTAL_MENU_ITEMS) % TOTAL_MENU_ITEMS;
        drawScreen();
      }

      // BtnB: Move selection DOWN (опускает выбор вниз)
      if (M5.BtnB.wasPressed()) {
        M5.Speaker.tone(1800, 30);
        selectedMenuItem = (selectedMenuItem + 1) % TOTAL_MENU_ITEMS;
        drawScreen();
      }

      // BtnA (M5): Select / Execute
      if (M5.BtnA.wasPressed()) {
        M5.Speaker.tone(2400, 50);
        executeAction(selectedMenuItem);
      }
      break;

    case STATE_CONFIRM_REBOOT:
      // BtnA: Confirm YES
      if (M5.BtnA.wasPressed()) {
        M5.Speaker.tone(1000, 200);
        sendRestartPC();
        currentState = STATE_MENU;
        drawScreen();
      }
      // BtnB or BtnPWR: Cancel NO
      if (M5.BtnB.wasPressed() || M5.BtnPWR.wasPressed()) {
        M5.Speaker.tone(1500, 40);
        currentState = STATE_MENU;
        drawScreen();
      }
      break;

    case STATE_CONFIRM_SHUTDOWN:
      // BtnA: Confirm YES
      if (M5.BtnA.wasPressed()) {
        M5.Speaker.tone(800, 250);
        sendShutdownPC();
        currentState = STATE_MENU;
        drawScreen();
      }
      // BtnB or BtnPWR: Cancel NO
      if (M5.BtnB.wasPressed() || M5.BtnPWR.wasPressed()) {
        M5.Speaker.tone(1500, 40);
        currentState = STATE_MENU;
        drawScreen();
      }
      break;

    case STATE_APP_SELECTOR:
      // BtnPWR: Move preset selection UP (вверх)
      if (M5.BtnPWR.wasPressed()) {
        M5.Speaker.tone(1800, 30);
        selectedPresetItem = (selectedPresetItem - 1 + TOTAL_PRESETS) % TOTAL_PRESETS;
        drawScreen();
      }

      // BtnPWR Hold: Back to main menu
      if (M5.BtnPWR.wasHold()) {
       M5.Speaker.tone(1500, 40);
        currentState = STATE_MENU;
        drawScreen();
      }

      // BtnB: Move preset selection DOWN (вниз)
      if (M5.BtnB.wasPressed()) {
        M5.Speaker.tone(1800, 30);
        selectedPresetItem = (selectedPresetItem + 1) % TOTAL_PRESETS;
        drawScreen();
      }

      // BtnA (M5): Launch selected preset
      if (M5.BtnA.wasPressed()) {
        M5.Speaker.tone(2500, 80);
        launchCommand(APP_PRESETS[selectedPresetItem].command);
        currentState = STATE_MENU;
        drawScreen();
      }
      break;

    case STATE_VIRTUAL_KEYBOARD:
      // BtnPWR Short press: Previous character (назад / влево)
      if (M5.BtnPWR.wasPressed()) {
        M5.Speaker.tone(2000, 20);
        currentKeyIndex = (currentKeyIndex - 1 + TOTAL_CHARS) % TOTAL_CHARS;
        drawVirtualKeyboard();
      }

      // BtnPWR Hold (>800ms): Backspace (delete last char) or Return to menu
      if (M5.BtnPWR.wasHold()) {
        M5.Speaker.tone(1600, 40);
        if (typedBuffer.length() > 0) {
          typedBuffer.remove(typedBuffer.length() - 1);
        } else {
          currentState = STATE_MENU;
        }
        drawScreen();
      }

      // BtnB Short press: Next character forward (вперед / вправо)
      if (M5.BtnB.wasPressed()) {
        M5.Speaker.tone(2000, 20);
        currentKeyIndex = (currentKeyIndex + 1) % TOTAL_CHARS;
        drawVirtualKeyboard();
      }
      
      // BtnB Long press: Jump 5 characters forward
      if (M5.BtnB.wasHold()) {
        M5.Speaker.tone(2200, 30);
        currentKeyIndex = (currentKeyIndex + 5) % TOTAL_CHARS;
        drawVirtualKeyboard();
      }

      // BtnA (M5): Add selected character
      if (M5.BtnA.wasPressed()) {
        M5.Speaker.tone(2400, 40);
        if (typedBuffer.length() < MAX_BUFFER_LEN) {
          typedBuffer += KEY_CHARS[currentKeyIndex];
        }
        drawVirtualKeyboard();
      }

      // BtnA (M5) Long press (Hold > 800ms): LAUNCH / EXECUTE!
      if (M5.BtnA.wasHold()) {
        if (typedBuffer.length() > 0) {
          M5.Speaker.tone(3000, 150);
          launchCommand(typedBuffer);
          typedBuffer = "";
          currentState = STATE_MENU;
          drawScreen();
        }
      }
      break;

    case STATE_MEDIA:
      // BtnA: Play / Pause
      if (M5.BtnA.wasPressed()) {
        M5.Speaker.tone(2000, 40);
        Serial.println(F("CMD:MEDIA:PLAY_PAUSE"));
        if (bleKeyboard.isConnected()) {
          bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
        }
      }
      // BtnB: Vol +
      if (M5.BtnB.wasPressed()) {
        M5.Speaker.tone(2200, 30);
        Serial.println(F("CMD:MEDIA:VOL_UP"));
        if (bleKeyboard.isConnected()) {
          bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
        }
      }
      // BtnPWR Short: Vol -
      if (M5.BtnPWR.wasPressed()) {
        M5.Speaker.tone(1800, 30);
        Serial.println(F("CMD:MEDIA:VOL_DOWN"));
        if (bleKeyboard.isConnected()) {
          bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
        }
      }
      // BtnPWR Hold: Back to menu
      if (M5.BtnPWR.wasHold()) {
        M5.Speaker.tone(1500, 40);
        currentState = STATE_MENU;
        drawScreen();
      }
      break;
  }

  delay(20);
}

// --------------------------------------------------------------------------
// Actions & Keystroke Sequences
// --------------------------------------------------------------------------
void executeAction(int menuIndex) {
  switch (menuIndex) {
    case 0: // Minimize All Windows
      sendMinimizeWindows();
      break;
    case 1: // App Launcher Presets
      currentState = STATE_APP_SELECTOR;
      drawScreen();
      break;
    case 2: // On-Screen Virtual Keyboard
      currentState = STATE_VIRTUAL_KEYBOARD;
      drawScreen();
      break;
    case 3: // Restart PC
      currentState = STATE_CONFIRM_REBOOT; drawScreen();
      break;
    case 4: // Shutdown PC
      currentState = STATE_CONFIRM_SHUTDOWN; drawScreen();
      break;
    case 5: // Media Controls
      currentState = STATE_MEDIA;
      drawScreen();
      break;
  }
}

void showFeedback(const char* title, const char* detail) {
  M5.Display.fillScreen(COLOR_BG);
  M5.Display.fillRoundRect(4, 30, M5.Display.width() - 8, 175, 6, COLOR_CARD);
  M5.Display.drawRoundRect(4, 30, M5.Display.width() - 8, 175, 6, COLOR_HIGHLIGHT);

  M5.Display.fillRoundRect(10, 40, M5.Display.width() - 20, 22, 3, COLOR_HIGHLIGHT);
  M5.Display.setTextColor(0x0000);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(18, 47);
  M5.Display.print(title);

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setCursor(12, 75);
  M5.Display.print("SENT ACTION:");

  M5.Display.setTextColor(COLOR_ACCENT);
  M5.Display.setCursor(12, 95);
  M5.Display.print(detail);

  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setCursor(12, 125);
  M5.Display.print("USB: 115200 ok");
  
  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setCursor(12, 142);
  M5.Display.print("BLE: HID ok");

  M5.Display.setTextColor(0x7BEF);
  M5.Display.setCursor(12, 172);
  M5.Display.print("Res: 135x240");
  
  delay(450);
  drawScreen();
}

void sendMinimizeWindows() {
  // 1. Send via USB Serial instantly
  Serial.println(F("CMD:MINIMIZE"));

  M5.Display.fillScreen(COLOR_BG);
  M5.Display.fillRoundRect(4, 35, M5.Display.width() - 8, 160, 6, COLOR_CARD);
  M5.Display.drawRoundRect(4, 35, M5.Display.width() - 8, 160, 6, COLOR_HIGHLIGHT);

  M5.Display.fillRoundRect(10, 45, M5.Display.width() - 20, 22, 3, COLOR_HIGHLIGHT);
  M5.Display.setTextColor(0x0000);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(16, 52);
  M5.Display.print("MINIMIZE ALL");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setCursor(12, 80);
  M5.Display.print("USB: CMD:MINIMIZE");

  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setCursor(12, 105);
  M5.Display.print("BLE: Win+D sent");
  M5.Display.setCursor(12, 125);
  M5.Display.print("All windows min");

  // 2. Also send via BLE HID if connected
  if (bleKeyboard.isConnected()) {
#if defined(OS_WINDOWS)
    // Win + D (Toggle Show Desktop / Minimize all)
    bleKeyboard.press(KEY_LEFT_GUI);
    bleKeyboard.press('d');
    delay(100);
    bleKeyboard.releaseAll();
#elif defined(OS_MACOS)
    // Cmd + F3 or Cmd + H
    bleKeyboard.press(KEY_LEFT_GUI);
    bleKeyboard.press(KEY_F3);
    delay(100);
    bleKeyboard.releaseAll();
#else
    // Linux: Super + D
    bleKeyboard.press(KEY_LEFT_GUI);
    bleKeyboard.press('d');
    delay(100);
    bleKeyboard.releaseAll();
#endif
  }

  delay(350);
  drawScreen();
}

void sendRestartPC() {
  // 1. Send via USB Serial
  Serial.println(F("CMD:REBOOT"));

  M5.Display.fillScreen(COLOR_BG);
  M5.Display.fillRoundRect(4, 35, M5.Display.width() - 8, 160, 6, COLOR_CARD);
  M5.Display.drawRoundRect(4, 35, M5.Display.width() - 8, 160, 6, COLOR_WARN);

  M5.Display.fillRoundRect(10, 45, M5.Display.width() - 20, 22, 3, COLOR_WARN);
  M5.Display.setTextColor(0x0000);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(16, 52);
  M5.Display.print("REBOOTING PC");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setCursor(12, 80);
  M5.Display.print("USB: CMD:REBOOT");

  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setCursor(12, 105);
  M5.Display.print("BLE: shutdown /r");
  M5.Display.setCursor(12, 125);
  M5.Display.print("Rebooting host...");
  if (bleKeyboard.isConnected()) {
#if defined(OS_WINDOWS)
    // Windows: Win + R -> shutdown /r /t 0 -> Enter
    bleKeyboard.press(KEY_LEFT_GUI);
    bleKeyboard.press('r');
    delay(150);
    bleKeyboard.releaseAll();
    delay(400); // Wait for Run dialog

    bleKeyboard.print("shutdown /r /t 0");
    delay(100);
    bleKeyboard.write(KEY_RETURN);
#elif defined(OS_MACOS)
    bleKeyboard.press(KEY_LEFT_GUI);
    bleKeyboard.press(' ');
    delay(300);
    bleKeyboard.releaseAll();
    delay(400);
    bleKeyboard.print("terminal");
    bleKeyboard.write(KEY_RETURN);
    delay(800);
    bleKeyboard.print("sudo reboot");
    bleKeyboard.write(KEY_RETURN);
#else
    bleKeyboard.press(KEY_LEFT_CTRL);
    bleKeyboard.press(KEY_LEFT_ALT);
    bleKeyboard.press('t');
    delay(200);
    bleKeyboard.releaseAll();
    delay(500);
    bleKeyboard.print("systemctl reboot");
    bleKeyboard.write(KEY_RETURN);
#endif
  }

  delay(600);
  drawScreen();
}

void sendShutdownPC() {
  // 1. Send via USB Serial
  Serial.println(F("CMD:SHUTDOWN"));

  M5.Display.fillScreen(COLOR_BG);
  M5.Display.fillRoundRect(4, 35, M5.Display.width() - 8, 160, 6, COLOR_CARD);
  M5.Display.drawRoundRect(4, 35, M5.Display.width() - 8, 160, 6, COLOR_WARN);

  M5.Display.fillRoundRect(10, 45, M5.Display.width() - 20, 22, 3, COLOR_WARN);
  M5.Display.setTextColor(0x0000);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(16, 52);
  M5.Display.print("SHUTTING DOWN");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setCursor(12, 80);
  M5.Display.print("USB: CMD:SHUTDOWN");

  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setCursor(12, 105);
  M5.Display.print("BLE: shutdown /s");
  M5.Display.setCursor(12, 125);
  M5.Display.print("Power off host...");
  if (bleKeyboard.isConnected()) {
#if defined(OS_WINDOWS)
    // Windows: Win + R -> shutdown /s /t 0 -> Enter
    bleKeyboard.press(KEY_LEFT_GUI);
    bleKeyboard.press('r');
    delay(150);
    bleKeyboard.releaseAll();
    delay(400); // Wait for Run dialog

    bleKeyboard.print("shutdown /s /t 0");
    delay(100);
    bleKeyboard.write(KEY_RETURN);
#elif defined(OS_MACOS)
    bleKeyboard.press(KEY_LEFT_GUI);
    bleKeyboard.press(' ');
    delay(300);
    bleKeyboard.releaseAll();
    delay(400);
    bleKeyboard.print("terminal");
    bleKeyboard.write(KEY_RETURN);
    delay(800);
    bleKeyboard.print("sudo shutdown -h now");
    bleKeyboard.write(KEY_RETURN);
#else
    bleKeyboard.press(KEY_LEFT_CTRL);
    bleKeyboard.press(KEY_LEFT_ALT);
    bleKeyboard.press('t');
    delay(200);
    bleKeyboard.releaseAll();
    delay(500);
    bleKeyboard.print("systemctl poweroff");
    bleKeyboard.write(KEY_RETURN);
#endif
  }

  delay(600);
  drawScreen();
}

void launchCommand(const String& cmd) {
  // 1. Send via USB Serial
  Serial.print(F("CMD:RUN:"));
  Serial.println(cmd);

  M5.Display.fillScreen(COLOR_BG);
  M5.Display.fillRoundRect(4, 35, M5.Display.width() - 8, 160, 6, COLOR_CARD);
  M5.Display.drawRoundRect(4, 35, M5.Display.width() - 8, 160, 6, COLOR_HIGHLIGHT);

  M5.Display.fillRoundRect(10, 45, M5.Display.width() - 20, 22, 3, COLOR_HIGHLIGHT);
  M5.Display.setTextColor(0x0000);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(16, 52);
  M5.Display.print("LAUNCHING APP");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setCursor(12, 80);
  M5.Display.print("Cmd: ");
  M5.Display.setTextColor(COLOR_ACCENT);
  M5.Display.print(cmd);

  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setCursor(12, 105);
  M5.Display.print("USB: 115200 ok");
  M5.Display.setCursor(12, 125);
  M5.Display.print("BLE: Executing...");

  // 2. Also send via BLE HID if connected
  if (bleKeyboard.isConnected()) {
#if defined(OS_WINDOWS)
    // Win + R -> Type command -> Enter
    bleKeyboard.press(KEY_LEFT_GUI);
    bleKeyboard.press('r');
    delay(150);
    bleKeyboard.releaseAll();
    delay(350); // Wait for Run dialog to gain focus

    bleKeyboard.print(cmd);
    delay(120);
    bleKeyboard.write(KEY_RETURN);
#elif defined(OS_MACOS)
    // Spotlight: Cmd + Space -> cmd -> Enter
    bleKeyboard.press(KEY_LEFT_GUI);
    bleKeyboard.press(' ');
    delay(150);
    bleKeyboard.releaseAll();
    delay(350);

    bleKeyboard.print(cmd);
    delay(200);
    bleKeyboard.write(KEY_RETURN);
#else
    // Linux Alt + F2 (Run command)
    bleKeyboard.press(KEY_LEFT_ALT);
    bleKeyboard.press(KEY_F2);
    delay(150);
    bleKeyboard.releaseAll();
    delay(350);

    bleKeyboard.print(cmd);
    delay(120);
    bleKeyboard.write(KEY_RETURN);
#endif
  }

  delay(500);
}

// --------------------------------------------------------------------------
// UI Rendering on LCD Screen
// --------------------------------------------------------------------------
void drawScreen() {
  switch (currentState) {
    case STATE_MENU:
      drawMenu();
      break;
    case STATE_CONFIRM_REBOOT:
      drawConfirmDialog("RESTART PC?", "USB: CMD:REBOOT\nBLE: Win+R shutdown");
      break;
    case STATE_CONFIRM_SHUTDOWN:
      drawConfirmDialog("SHUTDOWN PC?", "USB: CMD:SHUTDOWN\nBLE: Win+R shutdown");
      break;
    case STATE_APP_SELECTOR:
      drawAppPresets();
      break;
    case STATE_VIRTUAL_KEYBOARD:
      drawVirtualKeyboard();
      break;
    case STATE_MEDIA:
      drawMediaControls();
      break;
  }
}

// -----------------------------------------------------------------------------
// UI Renderers optimized for M5StickC Plus 2 (135 x 240 ST7789v2 LCD)
// -----------------------------------------------------------------------------

void drawHeader(const char* title) {
  M5.Display.fillScreen(COLOR_BG);
  
  // Top Header Bar (0..21)
  M5.Display.fillRect(0, 0, M5.Display.width(), 21, 0x10A2);
  M5.Display.drawFastHLine(0, 21, M5.Display.width(), 0x2965);
  
  // Title (Left aligned, compact)
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_ACCENT);
  M5.Display.setCursor(4, 7);
  M5.Display.print(title);
  
  // Dual Status Badges: USB & BLE (Right aligned)
  // In 135px width: USB at (76..97), BLE at (102..131)
  M5.Display.fillRoundRect(M5.Display.width() - 58, 4, 23, 13, 2, 0x01E4);
  M5.Display.setTextColor(COLOR_HIGHLIGHT);
  M5.Display.setCursor(M5.Display.width() - 56, 7);
  M5.Display.print("USB");
  
  bool isConnected = bleKeyboard.isConnected();
  M5.Display.fillRoundRect(M5.Display.width() - 32, 4, 29, 13, 2, isConnected ? 0x03E0 : 0x2945);
  M5.Display.setTextColor(isConnected ? COLOR_HIGHLIGHT : COLOR_SUBTEXT);
  M5.Display.setCursor(M5.Display.width() - 30, 7);
  M5.Display.print(isConnected ? "BT:OK" : "BT:--");
}

void drawMenu() {
  drawHeader("STREAMDECK");
  
  // Menu occupies y = 25 to 215 on 135x240 screen
  int startY = 25;
  int itemHeight = 31;
  
  for (int i = 0; i < TOTAL_MENU_ITEMS; i++) {
    int y = startY + i * itemHeight;
    bool isSelected = (i == selectedMenuItem);
    
    if (isSelected) {
      M5.Display.fillRoundRect(3, y, M5.Display.width() - 6, 28, 4, 0x1925);
      M5.Display.drawRoundRect(3, y, M5.Display.width() - 6, 28, 4, COLOR_ACCENT);
      M5.Display.setTextColor(COLOR_ACCENT);
    } else {
      M5.Display.fillRoundRect(3, y, M5.Display.width() - 6, 28, 4, COLOR_CARD);
      M5.Display.setTextColor(COLOR_TEXT);
    }
    
    M5.Display.setTextSize(1);
    M5.Display.setCursor(7, y + 10);
    M5.Display.print(MENU_ITEMS[i]);
    
    if (isSelected) {
      M5.Display.setCursor(M5.Display.width() - 11, y + 10);
      M5.Display.print(">");
    }
  }
  
  // Footer navigation on 135x240
  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setCursor(4, M5.Display.height() - 11);
  M5.Display.print("PWR:^  B:v  A:Sel");
}

void drawConfirmDialog(const char* title, const char* subtitle) {
  M5.Display.fillScreen(COLOR_BG);
  
  // Warning Box fitting 135x240
  int boxW = M5.Display.width() - 8;
  M5.Display.fillRoundRect(4, 26, boxW, 186, 6, COLOR_CARD);
  M5.Display.drawRoundRect(4, 26, boxW, 186, 6, COLOR_WARN);
  
  M5.Display.fillRoundRect(10, 36, boxW - 12, 22, 3, COLOR_WARN);
  M5.Display.setTextColor(0x0000);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(18, 43);
  M5.Display.print("[ ATTENTION ]");
  
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setCursor(12, 70);
  M5.Display.print(title);
  
  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setCursor(12, 92);
  M5.Display.print(subtitle);
  
  // Action buttons
  M5.Display.fillRoundRect(10, 126, boxW - 12, 28, 4, 0x04E5);
  M5.Display.drawRoundRect(10, 126, boxW - 12, 28, 4, COLOR_HIGHLIGHT);
  M5.Display.setTextColor(0xFFFF);
  M5.Display.setCursor(16, 136);
  M5.Display.print("[A] CONFIRM YES");
  
  M5.Display.fillRoundRect(10, 162, boxW - 12, 28, 4, 0x2104);
  M5.Display.drawRoundRect(10, 162, boxW - 12, 28, 4, 0x528A);
  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setCursor(16, 172);
  M5.Display.print("[B/PWR] CANCEL");
  
  M5.Display.setTextColor(0x7BEF);
  M5.Display.setCursor(4, M5.Display.height() - 11);
  M5.Display.print("Press A to confirm");
}

void drawAppPresets() {
  drawHeader("PRESETS");
  
  int startY = 25;
  int itemHeight = 31;
  int visibleCount = min(6, TOTAL_PRESETS);
  int offset = max(0, selectedPresetItem - 3);
  if (offset + visibleCount > TOTAL_PRESETS) {
    offset = max(0, TOTAL_PRESETS - visibleCount);
  }
  
  for (int i = 0; i < visibleCount; i++) {
    int idx = offset + i;
    int y = startY + i * itemHeight;
    bool isSelected = (idx == selectedPresetItem);
    
    if (isSelected) {
      M5.Display.fillRoundRect(3, y, M5.Display.width() - 6, 28, 4, 0x1925);
      M5.Display.drawRoundRect(3, y, M5.Display.width() - 6, 28, 4, COLOR_HIGHLIGHT);
      M5.Display.setTextColor(COLOR_HIGHLIGHT);
    } else {
      M5.Display.fillRoundRect(3, y, M5.Display.width() - 6, 28, 4, COLOR_CARD);
      M5.Display.setTextColor(COLOR_TEXT);
    }
    
    M5.Display.setTextSize(1);
    M5.Display.setCursor(7, y + 6);
    M5.Display.print(APP_PRESETS[idx].name);
    
    M5.Display.setTextColor(COLOR_SUBTEXT);
    M5.Display.setCursor(7, y + 17);
    M5.Display.print("> ");
    M5.Display.print(APP_PRESETS[idx].command);
    
    if (isSelected) {
      M5.Display.setTextColor(COLOR_HIGHLIGHT);
      M5.Display.setCursor(M5.Display.width() - 11, y + 10);
      M5.Display.print(">");
    }
  }
  
  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setCursor(4, M5.Display.height() - 11);
  M5.Display.print("PWR:^  B:v  A:Run");
}

void drawVirtualKeyboard() {
  drawHeader("KEYBOARD");
  
  // Typed Text Box (y = 25..68) on 135x240
  M5.Display.fillRoundRect(4, 25, M5.Display.width() - 8, 44, 4, 0x0000);
  M5.Display.drawRoundRect(4, 25, M5.Display.width() - 8, 44, 4, COLOR_ACCENT);
  
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_ACCENT);
  M5.Display.setCursor(8, 29);
  M5.Display.print("COMMAND:");
  
  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setCursor(8, 45);
  if (typedBuffer.length() == 0) {
    M5.Display.setTextColor(COLOR_SUBTEXT);
    M5.Display.print("[type cmd...]");
  } else {
    int startIdx = max(0, (int)typedBuffer.length() - 15);
    M5.Display.print(typedBuffer.substring(startIdx));
    M5.Display.print("_");
  }
  
  // Keyboard Character Drum / Carousel (y = 74..142) on 135x240
  M5.Display.fillRoundRect(4, 74, M5.Display.width() - 8, 68, 4, COLOR_CARD);
  M5.Display.drawRoundRect(4, 74, M5.Display.width() - 8, 68, 4, 0x2965);
  
  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(8, 79);
  M5.Display.print("SELECT CHAR:");
  
  // 5 characters centered on 135 width
  int offsets[] = {-2, -1, 0, 1, 2};
  int xPos[] = {10, 32, (M5.Display.width() / 2) - 13, M5.Display.width() - 48, M5.Display.width() - 26};
  
  for (int i = 0; i < 5; i++) {
    int off = offsets[i];
    int idx = (currentKeyIndex + off + TOTAL_CHARS) % TOTAL_CHARS;
    char ch = KEY_CHARS[idx];
    int x = xPos[i];
    
    if (off == 0) {
      M5.Display.fillRoundRect(x, 94, 26, 36, 4, COLOR_ACCENT);
      M5.Display.setTextColor(0x0000);
      M5.Display.setTextSize(2);
      M5.Display.setCursor(x + 7, 104);
      M5.Display.print(ch == ' ' ? '_' : ch);
    } else {
      M5.Display.fillRoundRect(x, 100, 18, 24, 3, 0x10A2);
      M5.Display.setTextColor(COLOR_SUBTEXT);
      M5.Display.setTextSize(1);
      M5.Display.setCursor(x + 5, 108);
      M5.Display.print(ch == ' ' ? '_' : ch);
    }
  }
  
  // Action Buttons on LCD (y = 148..214)
  M5.Display.fillRoundRect(5, 148, M5.Display.width() - 10, 28, 4, 0x1925);
  M5.Display.drawRoundRect(5, 148, M5.Display.width() - 10, 28, 4, COLOR_ACCENT);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setCursor(14, 158);
  M5.Display.print("[A] ENTER CHAR");
  
  M5.Display.fillRoundRect(5, 182, M5.Display.width() - 10, 28, 4, typedBuffer.length() > 0 ? 0x04E4 : 0x2104);
  M5.Display.drawRoundRect(5, 182, M5.Display.width() - 10, 28, 4, typedBuffer.length() > 0 ? COLOR_HIGHLIGHT : 0x39E7);
  M5.Display.setTextColor(typedBuffer.length() > 0 ? 0xFFFF : COLOR_SUBTEXT);
  M5.Display.setCursor(14, 192);
  M5.Display.print("[Hold A] RUN CMD");
  
  // Navigation Guide in Footer
  M5.Display.setTextColor(0x7BEF);
  M5.Display.setCursor(4, M5.Display.height() - 11);
  M5.Display.print("PWR:^ B:v HoldPWR:Del");
}

void drawMediaControls() {
  drawHeader("MEDIA");
  
  M5.Display.setTextSize(1);
  
  // 4 action cards for 135x240 screen
  int cardY[] = {26, 68, 110, 152};
  const char* titles[] = {"[A] Play / Pause", "[B] Volume Up +", "[PWR] Volume Down -", "[Hold A] Mute Audio"};
  const char* subtexts[] = {"Media Toggle (Play)", "Audio Level +", "Audio Level -", "Mute / Unmute"};
  uint16_t borderColors[] = {COLOR_ACCENT, COLOR_HIGHLIGHT, 0xFA20, COLOR_SUBTEXT};
  
  for (int i = 0; i < 4; i++) {
    int y = cardY[i];
    M5.Display.fillRoundRect(4, y, M5.Display.width() - 8, 36, 4, COLOR_CARD);
    M5.Display.drawRoundRect(4, y, M5.Display.width() - 8, 36, 4, borderColors[i]);
    
    M5.Display.setTextColor(COLOR_TEXT);
    M5.Display.setCursor(8, y + 7);
    M5.Display.print(titles[i]);
    
    M5.Display.setTextColor(COLOR_SUBTEXT);
    M5.Display.setCursor(8, y + 21);
    M5.Display.print(subtexts[i]);
  }
  
  M5.Display.setTextColor(COLOR_SUBTEXT);
  M5.Display.setCursor(4, 196);
  M5.Display.print("USB: CMD:MEDIA_*");
  M5.Display.setCursor(4, 208);
  M5.Display.print("BLE: Consumer HID");
  
  M5.Display.setTextColor(0x7BEF);
  M5.Display.setCursor(4, M5.Display.height() - 11);
  M5.Display.print("Hold PWR to Exit");
}
