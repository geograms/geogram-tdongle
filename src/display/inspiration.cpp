#include "inspiration.h"
#include "display.h"
#include <Arduino.h>

const char* openers[] = {
    "Remember:",
    "Stay alert:",
    "Off the grid:",
    "Pro tip:",
    "Don't forget:",
    "In silence:",
    "Your system:",
    "Legacy fades:",
    "Think different:",
    "Trust yourself:"
};

const char* topics[] = {
    "every sensor is a signal",
    "power is independence",
    "trust is earned, not delegated",
    "own your comms",
    "air-gapped is peace of mind",
    "the network is hostile",
    "privacy is a survival skill",
    "centralized systems fail big",
    "decentralization is resilience",
    "freedom begins at the edge",
    "encryption is self-defense",
    "logs are forever",
    "your device speaks for you",
    "low power is stealth mode",
    "no cloud knows your truth",
    "packets reveal patterns",
    "offline is a valid state",
    "resilience comes from preparation",
    "broadcast less, observe more",
    "entropy feeds security",
    "digital silence is strength",
    "permissions are attack surfaces",
    "code is law, unless audited",
    "signal-to-noise is strategy"
};

const char* endings[] = {
    ".",
    " >> stay resilient.",
    " >> be unpredictable.",
    " >> reduce surface area.",
    " >> question all defaults.",
    " >> stay silent, stay strong.",
    " >> you are your last line of defense.",
    " >> visibility is vulnerability.",
    " >> obscure, confuse, mislead.",
    " >> carry less, trust less.",
    " >> your system, your rules.",
    " >> hide in plain sight.",
    " >> encrypt locally, transmit wisely.",
    " >> sensors don't sleep.",
    " >> reset assumptions daily.",
    " >> trust the mesh.",
    " >> blend into noise.",
    " >> avoid digital footprints."
};

void generateInspiration() {
    const int openerCount = sizeof(openers) / sizeof(openers[0]);
    const int topicCount = sizeof(topics) / sizeof(topics[0]);
    const int endingCount = sizeof(endings) / sizeof(endings[0]);

    String message;
    message += openers[random(openerCount)];
    message += " ";
    message += topics[random(topicCount)];
    message += endings[random(endingCount)];

    //writeLog(message.c_str());
}
