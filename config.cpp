#include "config.h"
#include "globals.h"

void loadConfig() {
  prefs.begin("motor", true);
  if (prefs.isKey("cfg")) {
    prefs.getBytes("cfg", &config, sizeof(config));
    // if config params change, we need a version to keep track
    if (config.version != CONFIG_VERSION) setDefaultConfig();
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
  config.numPasses = 0;
}
