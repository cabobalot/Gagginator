#include "Menu.h"
#include "pins.h" 

Menu menu;
Menu* Menu::_self = nullptr;
const char* Menu::NVS_NAMESPACE = "espresso";  // NVS namespace

Menu::Menu() : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET, 800000UL, 800000UL) {}

bool Menu::begin() {
    Wire.begin(PIN_SCREEN_SDA, PIN_SCREEN_SCL);
    Wire.setClock(800000UL);
    if (!display.begin(0x3C, true)) {
        return false;
    }
    display.cp437(true);
    return true;
}
bool Menu::beginInput(int pinA, int pinB, int pinBtn) {
    _self = this;
    _pinA = pinA; _pinB = pinB; _pinBtn = pinBtn;
    
    pinMode(_pinA, INPUT);
    pinMode(_pinB, INPUT);
    pinMode(_pinBtn, INPUT_PULLUP);
    
    attachInterrupt(digitalPinToInterrupt(_pinA), Menu::isrAWrapper, CHANGE);
    return true;
}

void IRAM_ATTR Menu::isrAWrapper() {
    if (_self) _self->onAChangeISR();
}

void IRAM_ATTR Menu::onAChangeISR() {
    bool a = digitalRead(_pinA);
    bool b = digitalRead(_pinB);
    _encUp   = a ? b : !b;
    _encFired = true;
}

// input handling, call frequently from main loop 
void Menu::update() {
    pollInput();

    int step = consumeStep();
    if (step != 0) moveSelection(step > 0);
    if (consumeClick()) select();
}

void Menu::pollInput() {
    // === Encoder accumulation (keep original logic) ===
    if (_encFired) {
        _stepAccum += (_encUp ? +1 : -1);
        _encFired = false;
    }
    
    // === Button: edge detection + debounce, count only once on stable "release->press" transition ===
    unsigned long now = millis();
    bool rawPressed = !digitalRead(_pinBtn);   // Pull-up input: LOW = pressed
    
    // Record the time when raw reading edge occurs
    if (rawPressed != _btnPrevRaw) {
        _btnPrevRaw    = rawPressed;
        _btnEdgeTimeMs = now;
    }
    
    // Update "stable state" only after raw state is stable beyond debounce time
    if ((now - _btnEdgeTimeMs) >= DEBOUNCE_MS ) {
        if (rawPressed != _btnStable) {
            _btnStable = rawPressed;
            
            if (_btnStable) {               // Stable transition from "not pressed" -> "pressed"
                if (!_btnLatched) {         // Count only if not already counted
                    _clicked = true;        // One-time click event (read by consumeClick())
                    _btnLatched = true;     // Latch, prevent repeated counting until release
                    _pressCount++;
                    
                    // Optional: serial statistics
                    Serial.print("Button press #");
                    Serial.println(_pressCount);
                }
            } else {                         // Stable transition from "pressed" -> "released"
                _btnLatched = false;        // Unlatch, allow next press to be counted
            }
        }
    }
}

int Menu::consumeStep() {
    if (abs(_stepAccum) < 2) { // new encoder outputs two steps per step 
        return 0;
    }
    int s = _stepAccum;
    _stepAccum = 0;
    return s;
}

bool Menu::consumeClick() {
    bool c = _clicked;
    _clicked = false;
    return c;
}
//State control
//press the button to change state
void Menu::setState(MenuState s) {
    currentState = s;
    
    // Reset listSelection and scrollOffset to avoid cross-page state residue
    listSelection = 0;
    scrollOffset = 0;
    
    
    if (s == STEAM_PAGE) {
        steamStartTime = millis();
        lastFrameTime = 0;
        currentFrame = 0;
    }else if (s == BREW_PAGE) {
    brewStartTime = millis();
    lastFrameTime = 0;
    currentFrame  = 0;
    }else if (s == WATER_PAGE) {               
        waterStartTime = millis();
        lastFrameTime = 0;
        currentFrame  = 0;
    }
}

void Menu::resetBrewAnimation() {
    brewStartTime  = millis();
    lastFrameTime  = 0;
    currentFrame   = 0;
}
void Menu::show() {
    display.clearDisplay();
    if (currentState == MAIN_MENU) {
        showMainMenu();
        showSidebarInfo();
    } else if (currentState == MODE_PAGE) {
        showModePage();
    } else if (currentState == SETTING_PAGE) {
        showSettingPage();
        showSidebarInfo();
    } else if (currentState == PROFILE_PAGE) {
        showProfilePage();
    } else if (currentState == BREW_PAGE){
        showBrewPage();
    } else if (currentState == STEAM_PAGE){
        showSteamPage();
    }else if (currentState == WATER_PAGE){
        showWaterPage();
    }

    display.display();
}

void Menu::moveSelection(bool up) {
    if (currentState == SETTING_PAGE && isEditingBrewTemperature) {
            if (up && brewTemperature < 150) brewTemperature++;
            if (!up && brewTemperature > 30) brewTemperature--;
            // Don't save during editing, only save when exiting edit mode
            return;
        }
    
    if (currentState == SETTING_PAGE && isEditingSteamTemperature) {
            if (up && steamTemperature < 150) steamTemperature++;
            if (!up && steamTemperature > 50) steamTemperature--;
            // Don't save during editing, only save when exiting edit mode
            return;
        }

    // Editing Brew Pressure
    if (currentState == SETTING_PAGE && isEditingPressureBrew) {
        if (up  && targetPressurePsiBrew < 150.0f)  targetPressurePsiBrew += 1.0f;
        if (!up && targetPressurePsiBrew >   0.0f)  targetPressurePsiBrew -= 1.0f;
        // Don't save during editing, only save when exiting edit mode
        return;
    }
    // Editing Steam Percentage
    if (currentState == SETTING_PAGE && isEditingSteamPercentage) {
        if (up  && steamPercentage < 128) steamPercentage += 1;
        if (!up && steamPercentage > 1)   steamPercentage -= 1;
        // Don't save during editing, only save when exiting edit mode
        return;
    }
    // Editing: Preinf Pressure
    if (currentState == SETTING_PAGE && isEditingPreinfPressure) {
        if (up  && preinfPressurePsi < 150.0f) preinfPressurePsi += 0.5f;
        if (!up && preinfPressurePsi >   0.0f) preinfPressurePsi -= 0.5f;
        // Don't save during editing, only save when exiting edit mode
        return;
    }
    // Editing: Preinf Time
    if (currentState == SETTING_PAGE && isEditingPreinfTime) {
        if (up  && preinfTimeSec < 60) preinfTimeSec += 1;
        if (!up && preinfTimeSec >  0) preinfTimeSec -= 1;
        // Don't save during editing, only save when exiting edit mode
        return;
    }
    
    //main page
    if (currentState == MAIN_MENU) {
        if (up) {
            if (listSelection > 0) {
                listSelection--;
            }
        } else {
            if (listSelection < MAX_MENU_ITEMS - 1) {
                listSelection++;
            }
        }
    }
    //mode page
    else if (currentState == MODE_PAGE) { 
        if (up) {
            if (listSelection > 0) {
                listSelection--;
            }
        } else {
            if (listSelection < MAX_MODE_ITEMS - 1) {
                listSelection++;
            }
        }
    }
    //setting page
    else if (currentState == SETTING_PAGE) {  
        if (up) {
            if (listSelection > 0) {
                listSelection--;
            }
        } else {
            if (listSelection < MAX_SETTING_ITEMS - 1) {
                listSelection++;
            }
        }
      }
    //profile page
      else if (currentState == PROFILE_PAGE) {
        if (up) {
            if (listSelection > 0) {
                listSelection--;
            }
        } else {
            if (listSelection < MAX_PROFILE_ITEMS - 1) {
                listSelection++;
            }
        }
      }
 
    // Scroll logic (only effective for pages that need scrolling)
    // MODE_PAGE, SETTING_PAGE, PROFILE_PAGE don't use scrolling (all items can be displayed)
    if (currentState == MAIN_MENU) {
        if (listSelection < scrollOffset) {
            scrollOffset--;  // Scroll up
        } else if (listSelection >= scrollOffset + MAX_VISIBLE_ITEMS) {
            scrollOffset++;  // Scroll down
        }
    }
}
float Menu::getTargetTemperature() const {
    // Backward compatibility: return brew temperature (default)
    return (float)brewTemperature;
}
void Menu::setCurrentPressure(float psi) {
    currentPressurePsi = psi;
}
void Menu::setCurrentTemperature(float temp) {
    currentTemperature = temp;
}
void Menu::select() {
    if (currentState == MAIN_MENU) {
        if (listSelection == 2) {
            currentState = MODE_PAGE;
            listSelection = 0;
        } else if (listSelection == 0) {
            currentState = SETTING_PAGE;
            listSelection = 0;
        } else if (listSelection == 1) {
            currentState = PROFILE_PAGE;
            listSelection = 0;
        }
    } 
    else if (currentState == SETTING_PAGE) {
        if (listSelection == 0) {  // BrewTemp
            bool wasEditing = isEditingBrewTemperature;
            isEditingBrewTemperature = !isEditingBrewTemperature;
            if (isEditingBrewTemperature) {
                // Enter edit mode, save other items that might be in edit mode first
                if (isEditingSteamTemperature || isEditingPressureBrew || 
                    isEditingSteamPercentage || isEditingPreinfPressure || 
                    isEditingPreinfTime) {
                    saveSettings();
                }
                isEditingSteamTemperature = false;
                isEditingPressureBrew  = false;
                isEditingSteamPercentage = false;
                isEditingPreinfPressure = false;
                isEditingPreinfTime     = false;
            } else if (wasEditing) {
                // Exit edit mode, save settings
                saveSettings();
            }
        } else if (listSelection == 1) { // SteamTemp
            bool wasEditing = isEditingSteamTemperature;
            isEditingSteamTemperature = !isEditingSteamTemperature;
            if (isEditingSteamTemperature) {
                // Enter edit mode, save other items that might be in edit mode first
                if (isEditingBrewTemperature || isEditingPressureBrew || 
                    isEditingSteamPercentage || isEditingPreinfPressure || 
                    isEditingPreinfTime) {
                    saveSettings();
                }
                isEditingBrewTemperature = false;
                isEditingPressureBrew  = false;
                isEditingSteamPercentage = false;
                isEditingPreinfPressure = false;
                isEditingPreinfTime     = false;
            } else if (wasEditing) {
                // Exit edit mode, save settings
                saveSettings();
            }
        } else if (listSelection == 2) { // BrewPressure
            bool wasEditing = isEditingPressureBrew;
            isEditingPressureBrew = !isEditingPressureBrew;
            if (isEditingPressureBrew) {
                // Enter edit mode, save other items that might be in edit mode first
                if (isEditingBrewTemperature || isEditingSteamTemperature || 
                    isEditingSteamPercentage || isEditingPreinfPressure || 
                    isEditingPreinfTime) {
                    saveSettings();
                }
                isEditingBrewTemperature = false;
                isEditingSteamTemperature = false;
                isEditingSteamPercentage  = false;
                isEditingPreinfPressure = false;
                isEditingPreinfTime     = false;
            } else if (wasEditing) {
                // Exit edit mode, save settings
                saveSettings();
            }
        } else if (listSelection == 3) { // SteamPercentage
            bool wasEditing = isEditingSteamPercentage;
            isEditingSteamPercentage = !isEditingSteamPercentage;
            if (isEditingSteamPercentage) {
                // Enter edit mode, save other items that might be in edit mode first
                if (isEditingBrewTemperature || isEditingSteamTemperature || 
                    isEditingPressureBrew || isEditingPreinfPressure || 
                    isEditingPreinfTime) {
                    saveSettings();
                }
                isEditingBrewTemperature = false;
                isEditingSteamTemperature = false;
                isEditingPressureBrew   = false;
                isEditingPreinfPressure = false;
                isEditingPreinfTime     = false;
            } else if (wasEditing) {
                // Exit edit mode, save settings
                saveSettings();
            }
        } else if (listSelection == 4) { // PreinfPress
            bool wasEditing = isEditingPreinfPressure;
            isEditingPreinfPressure = !isEditingPreinfPressure;
            if (isEditingPreinfPressure) {
                // Enter edit mode, save other items that might be in edit mode first
                if (isEditingBrewTemperature || isEditingSteamTemperature || 
                    isEditingPressureBrew || isEditingSteamPercentage || 
                    isEditingPreinfTime) {
                    saveSettings();
                }
                isEditingBrewTemperature = false;
                isEditingSteamTemperature = false;
                isEditingPressureBrew   = false;
                isEditingSteamPercentage  = false;
                isEditingPreinfTime     = false;
            } else if (wasEditing) {
                // Exit edit mode, save settings
                saveSettings();
            }
        } else if (listSelection == 5) { // PreinfTime
            bool wasEditing = isEditingPreinfTime;
            isEditingPreinfTime = !isEditingPreinfTime;
            if (isEditingPreinfTime) {
                // Enter edit mode, save other items that might be in edit mode first
                if (isEditingBrewTemperature || isEditingSteamTemperature || 
                    isEditingPressureBrew || isEditingSteamPercentage || 
                    isEditingPreinfPressure) {
                    saveSettings();
                }
                isEditingBrewTemperature = false;
                isEditingSteamTemperature = false;
                isEditingPressureBrew   = false;
                isEditingSteamPercentage  = false;
                isEditingPreinfPressure = false;
            } else if (wasEditing) {
                // Exit edit mode, save settings
                saveSettings();
            }
        } else if (listSelection == 6) { // Default - only reset current profile to default values
            // Only reset current profile values, don't affect other profiles
            brewTemperature = 70;
            steamTemperature = 140;
            targetPressurePsiBrew = 40.0f;
            steamPercentage = 2;  // Default 2%
            preinfPressurePsi = 20.0f;
            preinfTimeSec = 5;
            // Don't change currentProfileId, keep currently selected profile
            // Save reset values to current profile
            saveSettings();
            Serial.println("Current profile reset to default values");
        } else if (listSelection == 7) { // Back
            // When exiting settings page, if still in edit mode, save first
            if (isEditingBrewTemperature || isEditingSteamTemperature || 
                isEditingPressureBrew || isEditingSteamPercentage || 
                isEditingPreinfPressure || isEditingPreinfTime) {
                saveSettings();
            }
            currentState = MAIN_MENU;
            isEditingBrewTemperature = false;
            isEditingSteamTemperature = false;
            isEditingPressureBrew   = false;
            isEditingSteamPercentage  = false;
            isEditingPreinfPressure = false;
            isEditingPreinfTime     = false;
            listSelection = 0;
            scrollOffset  = 0;
        }
    }
    else if (currentState == PROFILE_PAGE) {
        if (listSelection >= 0 && listSelection <= 3) {  // Profile 1-4
            uint8_t profileId = listSelection + 1;  // 1-4
            
            // Check if profile exists
            char key[32];
            snprintf(key, sizeof(key), "p%d_exists", profileId);
            bool profileExists = false;
            if (preferences.begin(NVS_NAMESPACE, true)) {
                profileExists = preferences.getBool(key, false);
                preferences.end();
            }
            
            // If profile doesn't exist, this is the first time selecting it, need to create it
            if (!profileExists) {
            // Create new profile with default values
            brewTemperature = 70;
            steamTemperature = 140;
            targetPressurePsiBrew = 40.0f;
            steamPercentage = 2;  // Default 2%
            preinfPressurePsi = 20.0f;
            preinfTimeSec = 5;
                // Set currentProfileId to new profile
                currentProfileId = profileId;
                // Save default settings to new profile (create profile)
                saveProfile(profileId);
                Serial.print("Profile ");
                Serial.print(profileId);
                Serial.println(" created for the first time with default values");
            } else {
                // Profile exists, load it
                currentProfileId = profileId;  // Set currentProfileId first
                loadProfile(profileId);
            }
            
            saveCurrentProfileId();  // Save current profile ID
            currentState = MAIN_MENU;
            listSelection = 0;
            scrollOffset = 0;
        } else if (listSelection == 4) {  // Back
            currentState = MAIN_MENU;
            listSelection = 0;
            scrollOffset = 0;
        }
    }
    else if (currentState == MODE_PAGE) {
        if (listSelection == 0) {          // Steam
            currentState = STEAM_PAGE;     // Switch to Steam page
        } else if (listSelection == 1) {  // Brew
            currentState = BREW_PAGE;    // Switch state
            brewStartTime = millis();
        } else if (listSelection == 2) {   // Water
            currentState = WATER_PAGE;     // New
            waterStartTime = millis();
        } else if (listSelection == 3) {  
            // Select Back to return to main menu
            currentState = MAIN_MENU;
            listSelection = 0;
            scrollOffset = 0;
        }
    }
}
//Show child page
void Menu::showMainMenu() {
    display.setCursor(0, 0);
    display.setTextSize(1);
    for (int i = 0; i < MAX_VISIBLE_ITEMS; i++) {
        int itemIndex = scrollOffset + i;  // Calculate current menu item index to display
        
        if (itemIndex < MAX_MENU_ITEMS) {
            if (itemIndex == listSelection) {
                display.setTextColor(SH110X_BLACK, SH110X_WHITE);  // Invert color for selected item
            } else {
                display.setTextColor(SH110X_WHITE);
            }
            display.println(menuItems[itemIndex]);
        }
    }
}
void Menu::showModePage() {
    display.setCursor(0, 0);
    display.setTextSize(1);
    // Display all 4 mode options, no scrolling
    for (int i = 0; i < MAX_MODE_ITEMS; i++) {
        if (i == listSelection) {
            display.setTextColor(SH110X_BLACK, SH110X_WHITE);
        } else {
            display.setTextColor(SH110X_WHITE);
        }
        display.println(modeMenuItems[i]);
    }
}

void Menu::showSettingPage() {
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    
    for (int i = 0; i < MAX_SETTING_ITEMS; i++) {
        if (i == listSelection) {
            display.setTextColor(SH110X_BLACK, SH110X_WHITE);  // Invert color for selected item
        } else {
            display.setTextColor(SH110X_WHITE);
        }

        if (i == 0) {  // BrewTemp 
            display.print("BrewT: ");
            display.print(brewTemperature);
            display.println("C");
        } else if (i == 1) {       // SteamTemp
            display.print("SteamT: ");
            display.print(steamTemperature);
            display.println("C");
        } else if (i == 2) {       // BrewPressure
            display.print("BrewP: ");
            display.print(targetPressurePsiBrew, 0); //decimal
            display.println(" P");
        } else if (i == 3) {       // SteamPercentage
            display.print("Steam%: ");
            display.print(steamPercentage);
            display.println("%");
        } else if (i == 4) {  // PreinfPress
            display.print("PreP: ");
            display.print(preinfPressurePsi, 1);
            display.println(" P");
        } else if (i == 5) {  // PreinfTime
            display.print("PreT: ");
            display.print(preinfTimeSec);
            display.println(" S");
        } else if (i == 6) {  // Default
            display.println("Default");
        } else if (i == 7) {  // Back
            display.println("Back");
        }
    }
}

void Menu::showProfilePage() {
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    
    for (int i = 0; i < MAX_PROFILE_ITEMS; i++) {
        if (i == listSelection) {
            display.setTextColor(SH110X_BLACK, SH110X_WHITE);  // Invert color for selected item
        } else {
            display.setTextColor(SH110X_WHITE);
        }
        
        // * indicates currently selected profile (currentProfileId)
        if (i >= 0 && i <= 3) {
            uint8_t profileId = i + 1;
            
            display.print(profileMenuItems[i]);
            // Only show * for currently selected profile
            if (currentProfileId == profileId) {
                display.print(" *");  // Mark currently selected profile
            }
            display.println();
        } else {
            display.println(profileMenuItems[i]);
        }
    }
}

void Menu::showSidebarInfo() {
    display.setTextColor(SH110X_WHITE); 
    display.drawLine(70, 0, 70, SCREEN_HEIGHT, SH110X_WHITE);
    display.setCursor(73, 0);
    display.print("Temp");
    
    display.setCursor(73, 10);
    display.print("Tar:");
    display.print(brewTemperature);
    display.println("C");
    
    display.setCursor(73, 20);
    display.print("C:");
    display.print(currentTemperature);
    display.println("C");
    
    display.drawLine(70, 30, SCREEN_WIDTH, 30, SH110X_WHITE);
    
    display.setTextColor(SH110X_WHITE);
    display.setCursor(73, 40);
    display.print("PSI:");
    display.print(currentPressurePsi, 1);

    display.println();
    display.setCursor(73, 50);
    display.print("Time: ");
    display.print(lastBrewTime);
    display.println("s");
}
void Menu::showBrewPage() {
    unsigned long now = millis();
    if (now - lastFrameTime > frameInterval) {
        currentFrame = (currentFrame + 1) % 2;  // Cycle between two frames
        lastFrameTime = now;
    }
    
    const unsigned char* frame = (currentFrame == 0) ? brew_coffee__ : brew_coffee__1;
    // Display image (centered)
    display.drawBitmap(0, 0, frame, 58, 64, SH110X_WHITE);
    
    // Countdown calculation
    display.setCursor(70, 0);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.println("BREW");

    // uint32_t sec = (now - brewStartTime) / 1000;
    lastBrewTime = (now - brewStartTime) / 1000;
    display.setCursor(70, 12);
    display.print("Time: ");
    display.print(lastBrewTime);
    display.println("s");

    display.setCursor(70, 24);
    display.print("Tar: ");
    display.print(brewTemperature);
    display.println("C");

    display.setCursor(60, 36);
    display.print("Cur: ");
    display.print(currentTemperature);
    display.println("C");

    display.setCursor(60, 48);
    display.print("Cur :");
    display.print(currentPressurePsi);
    display.println("P");
}

void Menu::showSteamPage() {
    unsigned long now = millis();
    if (now - lastFrameTime >= frameInterval) {
        currentFrame = (currentFrame + 1) % 2;
        lastFrameTime = now;
    }
    
    const unsigned char* frame = (currentFrame == 0) ? steam_1 : steam_2;
    display.drawBitmap(0, 0, frame, 58, 64, SH110X_WHITE);
    
    display.setCursor(70, 0);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.println("STEAM");
    
    uint32_t sec = (now - steamStartTime) / 1000;
    display.setCursor(70, 12);
    display.print("Time: ");
    display.print(sec);
    display.println("s");

    display.setCursor(70, 24);
    display.print("Tar: ");
    display.print(steamTemperature);
    display.println("C");

    display.setCursor(57, 36);
    display.print("CurT:");
    display.print(currentTemperature);
    display.println("C");

    display.setCursor(57, 48);
    display.print("CurP:");
    display.print(currentPressurePsi);
    display.println("P");
}

void Menu::showWaterPage() {
    unsigned long now = millis();
    if (now - lastFrameTime >= frameInterval) {
        currentFrame = (currentFrame + 1) % 2;
        lastFrameTime = now;
    }

    display.drawBitmap(0, 0, steam_2, 58, 64, SH110X_WHITE);

    display.setCursor(70, 0);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.println("WATER");

    uint32_t sec = (now - waterStartTime) / 1000;
    display.setCursor(70, 12);
    display.print("Time: ");
    display.print(sec);
    display.println("s");

    display.setCursor(70, 24);
    display.print("Tar: ");
    display.print(brewTemperature);
    display.println("C");

    display.setCursor(57, 36);
    display.print("CurT:");
    display.print(currentTemperature);
    display.println("C");

    display.setCursor(57, 48);
    display.print("CurP:");
    display.print(currentPressurePsi);
    display.println("P");
}

// ==================== Settings Persistence Storage ====================

void Menu::saveSettings() {
    // Save to current profile (if current is profile 0, save to default settings)
    Serial.print("saveSettings() called, currentProfileId=");
    Serial.println(currentProfileId);
    Serial.print("Current values to save: brewTemp=");
    Serial.print(brewTemperature);
    Serial.print(", steamTemp=");
    Serial.print(steamTemperature);
    Serial.print(", brewP=");
    Serial.print(targetPressurePsiBrew);
    Serial.print(", steam%=");
    Serial.print(steamPercentage);
    Serial.print(", preinfP=");
    Serial.print(preinfPressurePsi);
    Serial.print(", preinfT=");
    Serial.println(preinfTimeSec);
    saveProfile(currentProfileId);
    Serial.println("saveSettings() completed");
}

void Menu::loadSettings() {
    // First load the last used profile ID
    loadCurrentProfileId();
    Serial.print("loadSettings() - Loaded currentProfileId: ");
    Serial.println(currentProfileId);
    
    // If first time opening (currentProfileId is 0 or invalid), use profile 1
    if (currentProfileId == 0 || currentProfileId > 4) {
        currentProfileId = 1;
        Serial.println("First time opening, using profile 1");
    }
    
    // Load corresponding profile (use default values if doesn't exist)
    loadProfile(currentProfileId);
}

void Menu::saveProfile(uint8_t profileId) {
    // profileId must be 1-4, profile 0 is no longer used
    if (profileId == 0 || profileId > 4) {
        Serial.print("Invalid profileId: ");
        Serial.println(profileId);
        return;
    }
    
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        Serial.println("Failed to open preferences for saving!");
        return;
    }
    
    // Build key names: use "pX_xxx" format
    char key[32];
    
    Serial.print("Saving to profile ID: ");
    Serial.println(profileId);
    Serial.print("Values: brewTemp=");
    Serial.print(brewTemperature);
    Serial.print(", steamTemp=");
    Serial.print(steamTemperature);
    Serial.print(", brewP=");
    Serial.print(targetPressurePsiBrew);
    Serial.print(", steam%=");
    Serial.print(steamPercentage);
    Serial.print(", preinfP=");
    Serial.print(preinfPressurePsi);
    Serial.print(", preinfT=");
    Serial.println(preinfTimeSec);
    
    // Save user profiles (profileId 1-4)
    bool allSuccess = true;
    bool result;
    
    snprintf(key, sizeof(key), "p%d_brewTemp", profileId);
    result = preferences.putUChar(key, brewTemperature);
    if (!result) {
        Serial.print("ERROR: Failed to save ");
        Serial.println(key);
        allSuccess = false;
    }
    
    snprintf(key, sizeof(key), "p%d_steamTemp", profileId);
    result = preferences.putUChar(key, steamTemperature);
    if (!result) {
        Serial.print("ERROR: Failed to save ");
        Serial.println(key);
        allSuccess = false;
    }
    
    snprintf(key, sizeof(key), "p%d_brewP", profileId);
    result = preferences.putFloat(key, targetPressurePsiBrew);
    if (!result) {
        Serial.print("ERROR: Failed to save ");
        Serial.println(key);
        allSuccess = false;
    }
    
    snprintf(key, sizeof(key), "p%d_steamPct", profileId);
    result = preferences.putUChar(key, steamPercentage);
    if (!result) {
        Serial.print("ERROR: Failed to save ");
        Serial.println(key);
        allSuccess = false;
    }
    
    snprintf(key, sizeof(key), "p%d_preinfP", profileId);
    result = preferences.putFloat(key, preinfPressurePsi);
    if (!result) {
        Serial.print("ERROR: Failed to save ");
        Serial.println(key);
        allSuccess = false;
    }
    
    snprintf(key, sizeof(key), "p%d_preinfT", profileId);
    result = preferences.putUShort(key, preinfTimeSec);
    if (!result) {
        Serial.print("ERROR: Failed to save ");
        Serial.println(key);
        allSuccess = false;
    }
    
    // Mark profile as existing
    snprintf(key, sizeof(key), "p%d_exists", profileId);
    result = preferences.putBool(key, true);
    if (!result) {
        Serial.print("ERROR: Failed to save ");
        Serial.println(key);
        allSuccess = false;
    }
    
    if (allSuccess) {
        Serial.print("Settings saved to profile ");
        Serial.println(profileId);
    } else {
        Serial.print("WARNING: Some values may have failed to save to profile ");
        Serial.println(profileId);
    }
    
    preferences.end();
    Serial.println("Preferences closed");
}

void Menu::loadProfile(uint8_t profileId) {
    // profileId must be 1-4, profile 0 is no longer used
    if (profileId == 0 || profileId > 4) {
        Serial.print("Invalid profileId for loading: ");
        Serial.println(profileId);
        // Use default values
        brewTemperature = 70;
        steamTemperature = 140;
        targetPressurePsiBrew = 40.0f;
        steamPercentage = 2;  // Default ~2%
        preinfPressurePsi = 20.0f;
        preinfTimeSec = 5;
        return;
    }
    
    Serial.print("loadProfile() called with profileId=");
    Serial.println(profileId);
    
    if (!preferences.begin(NVS_NAMESPACE, true)) {
        Serial.println("Failed to open preferences for loading!");
        return;
    }
    
    char key[32];
    
    // Load user profiles (profileId 1-4)
    snprintf(key, sizeof(key), "p%d_exists", profileId);
    bool profileExists = preferences.getBool(key, false);
    
    if (!profileExists) {
        // Profile doesn't exist, use default values, but keep currentProfileId unchanged
        Serial.print("Profile ");
        Serial.print(profileId);
        Serial.println(" does not exist, using default values");
        
        // Use default values
        brewTemperature = 70;
        steamTemperature = 140;
        targetPressurePsiBrew = 40.0f;
        steamPercentage = 2;  // Default ~2%
        preinfPressurePsi = 20.0f;
        preinfTimeSec = 5;
        
        // Keep currentProfileId unchanged (so setting page shows current profile)
        preferences.end();
        return;
    }
        
    // Profile exists, load its values
    // Backward compatibility: if "p%d_temp" key exists, load as brewTemp
    snprintf(key, sizeof(key), "p%d_temp", profileId);
    if (preferences.isKey(key)) {
        brewTemperature = preferences.getUChar(key, 70);
    } else {
        snprintf(key, sizeof(key), "p%d_brewTemp", profileId);
        brewTemperature = preferences.getUChar(key, 70);
    }
    
    snprintf(key, sizeof(key), "p%d_steamTemp", profileId);
    steamTemperature = preferences.getUChar(key, 140);
    
    snprintf(key, sizeof(key), "p%d_brewP", profileId);
    targetPressurePsiBrew = preferences.getFloat(key, 40.0f);
    

    // // Backward compatibility: if "p%d_steamP" key exists (PSI value), convert to percentage; otherwise load new percentage value
    // snprintf(key, sizeof(key), "p%d_steamP", profileId);
    // if (preferences.isKey(key)) {
    //     // Old data: PSI value, convert to percentage (simple mapping: 25 PSI -> 64 (50%))
    //     float oldSteamP = preferences.getFloat(key, 25.0f);
    //     preferences.remove(key);  // Remove old key after migration // this didnt work for some reason?
    //     steamPercentage = (uint8_t)((oldSteamP / 25.0f) * 64.0f);  // Simple conversion
    //     if (steamPercentage < 1) steamPercentage = 1;
    //     if (steamPercentage > 128) steamPercentage = 128;
    //     Serial.print("Migrated old steamP value ");
    //     Serial.print(oldSteamP);
    //     Serial.print(" to percentage ");
    //     Serial.println(steamPercentage);
    // } else {
        snprintf(key, sizeof(key), "p%d_steamPct", profileId);
        steamPercentage = preferences.getUChar(key, 2);  // Default 2%
        if (steamPercentage < 1) steamPercentage = 1;
        if (steamPercentage > 128) steamPercentage = 128;
    // }
    
    snprintf(key, sizeof(key), "p%d_preinfP", profileId);
    preinfPressurePsi = preferences.getFloat(key, 20.0f);
    
    snprintf(key, sizeof(key), "p%d_preinfT", profileId);
    preinfTimeSec = preferences.getUShort(key, 5);
    
    // Don't set currentProfileId here, caller has already set it
    
    Serial.print("Settings loaded from profile ");
    Serial.println(profileId);
    
    preferences.end();
}

void Menu::resetToDefaults() {
    // Restore all settings to default values (don't change currentProfileId)
    brewTemperature = 70;
    steamTemperature = 140;
    targetPressurePsiBrew = 40.0f;
    steamPercentage = 2;  // Default 2%
    preinfPressurePsi = 20.0f;
    preinfTimeSec = 5;
    // Don't change currentProfileId, keep currently selected profile
    
    Serial.println("Settings reset to defaults");
}

void Menu::markSettingsDirty() {
    settingsDirty = true;
    lastSaveTime = millis();
}

void Menu::checkAndSaveSettings() {
    if (!settingsDirty) {
        return;
    }
    
    // Save if delay time has passed since last modification
    unsigned long now = millis();
    if (now - lastSaveTime >= SAVE_DELAY_MS) {
        saveSettings();
        settingsDirty = false;
        Serial.println("Settings saved (delayed)");
    }
}

void Menu::saveCurrentProfileId() {
    if (!preferences.begin(NVS_NAMESPACE, false)) {
        Serial.println("Failed to open preferences for saving profile ID!");
        return;
    }
    
    preferences.putUChar("currentProfileId", currentProfileId);
    preferences.end();
    
    Serial.print("Current profile ID saved: ");
    Serial.println(currentProfileId);
}

void Menu::loadCurrentProfileId() {
    if (!preferences.begin(NVS_NAMESPACE, true)) {
        Serial.println("Failed to open preferences for loading profile ID!");
        return;
    }
    
    currentProfileId = preferences.getUChar("currentProfileId", 0);
    preferences.end();
    
    Serial.print("Current profile ID loaded: ");
    Serial.println(currentProfileId);
}

