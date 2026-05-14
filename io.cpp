#include "io.h"
#include "globals.h"

void getPasses(JsonArray passes) {
	for (int i = 0; i < config.numPasses; ++i) {
		JsonObject pass = passes.add<JsonObject>();

		pass["rpm"] = config.passes[i].rpm;
		pass["accel"] = config.passes[i].accel;
		pass["delay"] = config.passes[i].delay;
	}
}

void getPasses(Stream& stream) {
	JsonDocument doc;

	getPasses(doc.to<JsonArray>());
	serializeJson(doc, stream);
}

void getJSON(Stream& stream) {
	JsonDocument doc;

	doc["freq"] = config.frequency;
	doc["duty"] = config.dutyCycle;
	doc["move"] = config.length;
	doc["pwmdur"] = config.duration;
	doc["pwmpin"] = config.pwmPin;

	getPasses(doc["passes"].to<JsonArray>());
	serializeJson(doc, stream);
}
