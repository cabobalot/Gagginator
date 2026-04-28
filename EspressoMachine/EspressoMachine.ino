#include "Menu.h"
#include "bitmaps.h"
#include "pressure_sensor.h"
#include "temp_sensor.h"
#include "pins.h"
#include "pressure_control.h" 
#include "tempControl.h" 
#include "dataWebPage.h"
#include "EspressoMachine.h"

#include "timingTestDebug.h"


#define TEMPERATURE_OFFSET 7 // stock calibration offset

TemperatureSensor tempSensor;
PressureControl pc(5, 0.0, 0.1);
// also test in steam mode

TaskHandle_t mainTask;
TaskHandle_t uiTask;
TaskHandle_t screenShowTask;

void mainLoop(void * pvParameters);
void uiLoop(void * pvParameters);
void screenShowLoop(void * pvParameters);

static volatile float targetTemperature = 95;
static volatile float targetPressureBrew  = 120.0f;
static volatile uint8_t steamPercentage = 2;  // 1-128, default 2%
static volatile float currentTemperature = 0;
static volatile float currentPressure = 0;

static volatile float preinfusePressure = 25;
static volatile unsigned long preinfuseTime = 10;

// Brew mode pressure control: record brew start time for switching between preinfusion and normal pressure
static volatile unsigned long brewStartTimeMs = 0;
static volatile float currentTargetPressure = 0.0f;  // Currently used target pressure (for serial output)


static volatile MachineState machineState = IDLE_STATE;


void setup() {
  Serial.begin(115200);

  if (!menu.begin()) { // doing this here so we have serial if it fails
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  dataWebPage::init(); // I hope this doesnt care which core it happens on

  xTaskCreatePinnedToCore(uiLoop, "uiLoop", 10000, NULL, 1, &uiTask, 1);
  xTaskCreatePinnedToCore(mainLoop, "mainLoop", 10000, NULL, 1, &mainTask, 1);
  xTaskCreatePinnedToCore(screenShowLoop, "screenShowLoop", 10000, NULL, 1, &screenShowTask, 0);
}

void screenShowLoop(void * pvParameters) {
  for(;;) {
    menu.show();
    vTaskDelay(50 / portTICK_PERIOD_MS); // 20 FPS max
  }
}

void uiLoop(void * pvParameters) {
  // --- debounce state ---
  static bool     lastBrewRaw  = false,  brewStable  = false;
  static bool     lastSteamRaw = false,  steamStable = false;
  static uint32_t brewEdgeMs   = 0,      steamEdgeMs = 0;
  const  uint16_t DEBOUNCE_MS  = 30;

  menu.beginInput(PIN_KNOB_ROTATE_A, PIN_KNOB_ROTATE_B, PIN_KNOB_BUTTON); // pins an interrupt on this core

  pinMode(PIN_SWITCH_BREW, INPUT_PULLUP);
  pinMode(PIN_SWITCH_STEAM, INPUT_PULLUP);

  // Load saved settings
  menu.loadSettings();

  for(;;) {
    // ===================== UI input & rendering =====================

    menu.update();

    // Targets from UI
    // Select corresponding temperature based on current mode
    if (machineState == STEAM_STATE) {
      targetTemperature = menu.getTargetTemperatureSteam() + TEMPERATURE_OFFSET;
    } else {
      // BREW_STATE, IDLE_STATE, HOT_WATER_STATE use brew temperature
      targetTemperature = menu.getTargetTemperatureBrew() + TEMPERATURE_OFFSET;
    }
    targetPressureBrew  = menu.getTargetPressureBrew();
    steamPercentage = menu.getSteamPercentage();
    preinfusePressure   = menu.getPreinfPressure(); 
    preinfuseTime       = menu.getPreinfTimeSec();

    // Live values back to UI
    menu.setCurrentTemperature(currentTemperature - TEMPERATURE_OFFSET);
    menu.setCurrentPressure(currentPressure);

    // ===================== Switch handling (level-based, both edges) =====================
    // Active-low hardware switches: LOW = pressed
    bool rawBrew  = !digitalRead(PIN_SWITCH_BREW);
    bool rawSteam = !digitalRead(PIN_SWITCH_STEAM);
    uint32_t now  = millis();

    // --- Brew debounce ---
    if (rawBrew != lastBrewRaw) { lastBrewRaw = rawBrew; brewEdgeMs = now; }
    if ((now - brewEdgeMs) >= DEBOUNCE_MS && rawBrew != brewStable) {
      brewStable = rawBrew;
    }

    // --- Steam debounce ---
    if (rawSteam != lastSteamRaw) { lastSteamRaw = rawSteam; steamEdgeMs = now; }
    if ((now - steamEdgeMs) >= DEBOUNCE_MS && rawSteam != steamStable) {
      steamStable = rawSteam;
    }

    // --- Unified decision (priority: BOTH -> WATER, else single) ---
    MachineState nextState = machineState;
    if (brewStable && steamStable) {
      nextState = HOT_WATER_STATE;            // Water mode: both keys pressed
    } else if (brewStable) {
      nextState = BREW_STATE;                 // Only Brew key pressed
    } else if (steamStable) {
      nextState = STEAM_STATE;                // Only Steam key pressed
    } else {
      nextState = IDLE_STATE;                 // Neither pressed
    }

    // --- Apply transition once (and set the correct page) ---
    if (nextState != machineState) {
      machineState = nextState;

      switch (machineState) {
        case BREW_STATE:
          brewStartTimeMs = millis();  // Record brew start time
          menu.setState(BREW_PAGE);
          menu.resetBrewAnimation();
          break;
        case STEAM_STATE:
          menu.setState(STEAM_PAGE);
          break;
        case HOT_WATER_STATE:
          menu.setState(WATER_PAGE);          // Both keys pressed -> enter Water page
          break;
        case IDLE_STATE:
        default:
          menu.setState(MAIN_MENU);
          break;
      }
    }

    // yield();
    vTaskDelay(20 / portTICK_PERIOD_MS); // 

  }
}

void mainLoop(void * pvParameters) {
  //temperature
  tempControl::init();
  //presure
  pinMode(PIN_PRESSURE_SENSE, INPUT);
  pc.init(PIN_DIMMER_CONTROL, PIN_DIMMER_ZERO_CROSS); // this pins an interrupt to this core

  // init
  pc.setAlwaysOff();

  pinMode(PIN_SOLENOID, OUTPUT);
  digitalWrite(PIN_SOLENOID, LOW);


  for(;;) {
    switch (machineState) {
    case IDLE_STATE:
      pc.setAlwaysOff();
      digitalWrite(PIN_SOLENOID, LOW);
      currentTargetPressure = 0.0f;
      break;
    case BREW_STATE:
      {
        // Calculate brew elapsed time (milliseconds)
        unsigned long brewElapsedMs = millis() - brewStartTimeMs;
        unsigned long preinfuseTimeMs = preinfuseTime * 1000;  // Convert to milliseconds
        
        // Use preinfusion pressure (20 PSI) for first 5 seconds, then use 70 PSI
        if (brewElapsedMs < preinfuseTimeMs) {
          currentTargetPressure = preinfusePressure;  // 20 PSI
        } else {
          currentTargetPressure = targetPressureBrew;  // Switch to 70 PSI after 5 seconds
        }
        
        pc.setSetpoint(currentTargetPressure);
        digitalWrite(PIN_SOLENOID, HIGH);
      }
      break;
    case STEAM_STATE:
      // Steam mode uses percentage for direct control, doesn't use PID
      pc.setPercentage(steamPercentage);
      digitalWrite(PIN_SOLENOID, LOW);
      // currentTargetPressure is for display, can be set to percentage value (optional)
      currentTargetPressure = (float)steamPercentage;  // Display percentage value
      break;
    case HOT_WATER_STATE:
      pc.setAlwaysOn();
      digitalWrite(PIN_SOLENOID, LOW);
      break;
    }

    tempControl::setSetpoint(targetTemperature); // screen returns a different temperature for each mode
    currentTemperature = tempControl::getTemperature();

    currentPressure = pc.getPressure();

    pc.update();

    tempControl::update();

    dataWebPage::update(currentTemperature - TEMPERATURE_OFFSET, currentPressure, targetTemperature - TEMPERATURE_OFFSET, currentTargetPressure, machineState);
    
    static unsigned long timeLastPrint = 0;
    if (millis() - timeLastPrint >= 1000) {
      timeLastPrint = millis();

      // Serial.printf("temp:%.1f, tempset:%.1f, pres:%.1f, pressSet:%.1f\r\n", currentTemperature, targetTemperature, currentPressure, targetPressure);


      // Serial.print(">temp:");
      // Serial.print(currentTemperature, 2);
      // Serial.print(",tempset:");
      // Serial.print(targetTemperature, 2);
      // Serial.print(",pres:");
      // Serial.print(currentPressure, 2);
      // Serial.print(",pressSet:");
      // Serial.print(targetPressureBrew, 2);
      // Serial.print(",targetPress:");
      // Serial.println(currentTargetPressure, 2);

      // Serial.print("UI core: ");
      // Serial.print(xTaskGetCoreID(uiTask));
      // Serial.print(" main core: ");
      // Serial.println(xTaskGetCoreID(mainTask));

    }

    // pretty sure we dont need a taskDelay here but maybe
    yield();
  }
}

void loop() {

}
