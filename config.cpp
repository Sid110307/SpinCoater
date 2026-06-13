#include "config.h"
#include "globals.h"
#include "hardware.h"

void loadConfig() {
  prefs.begin("motor", true);
  if (prefs.isKey("cfg")) {
    size_t read = prefs.getBytes("cfg", &config, sizeof(config));
    // if config params change, we need a version to keep track
    if (read != sizeof(config) || config.version != CONFIG_VERSION || !isValidMotorPwmPin(config.pwmPin)) {
      // preference data corrupted or mismatched so clear and use defaults
      prefs.clear();
      setDefaultConfig();
    }
  }
  else setDefaultConfig();
  prefs.end();
}

void saveConfig() {
  prefs.begin("motor", false);
  prefs.putBytes("cfg", &config, sizeof(config));
  prefs.end();
}

void setDefaultConfig() {
  config.version = CONFIG_VERSION;
  config.frequency = DEFAULT_FREQ;
  config.dutyCycle = DEFAULT_DUTY;
  config.length = 0;
  config.duration = 0;
  config.pwmPin = DEFAULT_PWM_PIN;
  config.polePairs = DEFAULT_POLE_PAIRS;
  config.numPasses = 0;
}
