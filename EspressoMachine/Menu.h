#ifndef MENU_H
#define MENU_H

#include <Wire.h>
#include <Adafruit_GFX.h>
// #include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#include "bitmaps.h"
#include <Preferences.h>  // For persistent storage settings

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64  // OLED screen is 128x32
#define OLED_RESET -1

#define MAX_MENU_ITEMS 3  // Mode, Setting, Profile
#define MAX_VISIBLE_ITEMS 3  
#define MAX_MODE_ITEMS 4 // mode items
#define MAX_SETTING_ITEMS 8   // BrewTemp, SteamTemp, BrewPressure, SteamPressure, PreinfPress, PreinfTime, Default, Back
#define MAX_PROFILE_ITEMS 5   // Profile 1, Profile 2, Profile 3, Profile 4, Back

enum MenuState {
    MAIN_MENU,
    MODE_PAGE,
    SETTING_PAGE,
    PROFILE_PAGE,  // Profile selection page
    BREW_PAGE,//brew page
    STEAM_PAGE,//steam page
    WATER_PAGE
};

class Menu {
private:
    //Adafruit_SSD1306 display;
    Adafruit_SH1106G display;
    int8_t listSelection = 0;  // Currently selected menu item
    int8_t scrollOffset = 0;   // Scroll offset
    const char* menuItems[MAX_MENU_ITEMS] = {"Setting", "Profile", "Mode"};
    const char* modeMenuItems[MAX_MODE_ITEMS] = {"Steam", "Brew", "Water", "Back"};
    const char* settingMenuItems[MAX_SETTING_ITEMS] = {"BrewTemp", "SteamTemp", "BrewPressure", "SteamPressure","PreinfPress", "PreinfTime", "Default", "Back"};
    const char* profileMenuItems[MAX_PROFILE_ITEMS] = {"Profile 1", "Profile 2", "Profile 3", "Profile 4", "Back"}; 
    MenuState currentState = MAIN_MENU; 
    //setter for pressure and temp
    uint8_t brewTemperature = 70;      // Target temperature for Brew mode
    uint8_t steamTemperature = 140;     // Target temperature for Steam mode (default 140 degrees)
    float currentTemperature = 0; 
    float currentPressurePsi = 0.0;
    //Default Value
    float targetPressurePsiBrew  = 40.0f;  // Default for brew pressure
    uint8_t steamPercentage = 2;  // Default for steam percentage (1-128), default 2%
    float   preinfPressurePsi = 20.0f;   // default 20 PSI
    uint16_t preinfTimeSec    = 5;      // default 5
    bool isEditingPreinfPressure = false;
    bool isEditingPreinfTime     = false;
    //Editing pressure
    bool  isEditingPressureBrew  = false;
    bool  isEditingSteamPercentage = false;
    bool isEditingBrewTemperature = false;
    bool isEditingSteamTemperature = false;
    //count for brew screen
    unsigned long brewStartTime = 0;
    unsigned long lastBrewTime = 0;
    unsigned long lastFrameTime = 0;
    // count for steam screen
    unsigned long steamStartTime = 0;
    //count for water page
    unsigned long waterStartTime = 0; 
    int currentFrame = 0;
    const unsigned long frameInterval = 500;

public:
    Menu();
    bool begin();
    void show();
    void moveSelection(bool up);
    void select();
    //prefinpressure and prefintime
    float   getPreinfPressure() const { return preinfPressurePsi; }
    uint16_t getPreinfTimeSec() const { return preinfTimeSec; }
    void    setPreinfPressure(float psi) { preinfPressurePsi = psi; }
    void    setPreinfTimeSec(uint16_t s) { preinfTimeSec = s; }
    //Temp
    void setCurrentTemperature(float temp);
    float getTargetTemperature() const;  // Return corresponding temperature based on current mode (backward compatibility)
    float getTargetTemperatureBrew() const { return (float)brewTemperature; }
    float getTargetTemperatureSteam() const { return (float)steamTemperature; }
    void setTargetTemperatureBrew(uint8_t temp) { brewTemperature = temp; }
    void setTargetTemperatureSteam(uint8_t temp) { steamTemperature = temp; }
    //pressure
    void setCurrentPressure(float psi);
    // Brew target pressure
    void  setTargetPressureBrew(float psi)  { targetPressurePsiBrew  = psi; }
    float getTargetPressureBrew() const     { return targetPressurePsiBrew; }
    // Steam percentage (1-128)
    void  setSteamPercentage(uint8_t pct)   { steamPercentage = pct; }
    uint8_t getSteamPercentage() const       { return steamPercentage; }
    //Encoder Function
    bool beginInput(int pinA, int pinB, int pinBtn);
    void update();
    void pollInput();
    bool consumeClick();  
    int  consumeStep(); 
    //State machine
    void showIdlePage();          // back to home page
    void showBrewPagePublic();    // brew page start pressure control
    void startBrewAnimation();    // start animation 
    void stopBrewAnimation(); 
    void showSteamPage();
    //Steam page
    void startSteamTimer() { steamStartTime = millis(); }
    //state control
    void setState(MenuState s);     // Only switch page state, don't draw directly
    void resetBrewAnimation();
    // Settings persistence storage
    void saveSettings();            // Save current settings to NVS (save immediately)
    void markSettingsDirty();       // Mark settings as modified (delayed save)
    void loadSettings();             // Load settings from NVS
    void checkAndSaveSettings();    // Check and save (if dirty)
    // User profile management
    void saveProfile(uint8_t profileId);  // Save current settings to specified profile
    void loadProfile(uint8_t profileId);  // Load settings from specified profile (use default if doesn't exist)
    void resetToDefaults();  // Restore default settings values
    uint8_t getCurrentProfileId() const { return currentProfileId; }
    void setCurrentProfileId(uint8_t id) { currentProfileId = id; }
    void saveCurrentProfileId();   // Save current profile ID
    void loadCurrentProfileId();    // Load current profile ID
      
private:
    void showSidebarInfo();
    void showMainMenu();
    void showModePage();
    void showSettingPage();
    void showTemperatureSetting();
    void showProfilePage();  // Display profile selection page
    void showBrewPage();
    void showWaterPage();
    // encoder
    int _pinA=-1, _pinB=-1, _pinBtn=-1;

    // ISR and numb
    static void IRAM_ATTR isrAWrapper();
    static Menu* _self;
    void IRAM_ATTR onAChangeISR();

    volatile bool _encFired=false;
    volatile bool _encUp=false;
    volatile int  _stepAccum=0;

    // Debounce state (new)
    bool     _btnPrevRaw    = false;
    bool     _btnStable     = false;
    bool     _btnLatched    = false;
    uint32_t _btnEdgeTimeMs = 0;
    uint32_t _pressCount    = 0;
    static constexpr uint16_t DEBOUNCE_MS = 15;
    bool _clicked=false;
    
    // Preferences storage
    Preferences preferences;
    static const char* NVS_NAMESPACE;  // NVS namespace
    uint8_t currentProfileId = 0;      // Currently used profile ID (0=default settings, 1-9=user profiles)
    bool settingsDirty = false;        // Mark if settings have been modified (for delayed save)
    unsigned long lastSaveTime = 0;    // Last save time
    static constexpr unsigned long SAVE_DELAY_MS = 2000;  // Delayed save time (save after 2 seconds without modification)
};

extern Menu menu;

#endif
