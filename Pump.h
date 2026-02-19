#ifndef PUMP_ERROR_HARDWARE
#define PUMP_ERROR_HARDWARE
#include <Arduino.h>

enum PumpError {
    PUMP_OK,
    PUMP_WARNING_TIMEOUT,
    PUMP_MANUAL_MODE,
    PUMP_ERROR_EMERGENCY,
    PUMP_ERROR_OVERRUN,
    PUMP_ERROR_OVERCURRENT,
    PUMP_ERROR_HARDWARE
};

class PumpControl {
    public:
    void begin(pin); //need pin
    void activate(unsigned int seconds);
    void deactivate();
    bool isActive();

    //Status stuff
    unsigned int getRemainingTime();
    PumpError checkFaults();

    //control
    void manualOverride(bool enable, unsigned int seconds = 30);
    void emergencyStop();

    //statistics
    void getStats(unsigned long &starts, unsigned long &runtime);
    void resetStats();

    //for modded pump
    void primePump();
    void safeShutdown();
    bool selfTest();


    private:
    uint8_t relayPin;
    bool running;
    unsigned long stopTime;
    
    //statistics
    unsigned long pumpStarts;
    unsigned long totalRuntime;

    //errors
    PumpError lastError;
};

#endif