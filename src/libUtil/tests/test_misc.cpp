#include "catch.hpp"

#include "BitOps.h"
#include "PirateSpeak.h"

TEST_CASE("PirateSpeak", "[PirateSpeak]") {
  REQUIRE(logging::piratify("my scan is starting") == "me voyage be settin' sail");
  // Case is preserved per-word (lower/upper/title)
  REQUIRE(logging::piratify("Welcome") == "Avast");
  REQUIRE(logging::piratify("WELCOME") == "AVAST");
  // Unknown words, punctuation, and numbers pass through unchanged
  REQUIRE(logging::piratify("Rd53b #42!") == "Rd53b #42!");
  // Frequent scan-log vocabulary is also mapped
  REQUIRE(logging::piratify("Loading config") == "Haulin' aboard logbook");
  REQUIRE(logging::piratify("Activated 48 pixels in one Core Column") ==
          "Hoisted 48 barnacles in one Keel Mast");

  REQUIRE_FALSE(logging::pirateModeEnabled());
  logging::setPirateMode(true);
  REQUIRE(logging::pirateModeEnabled());
  logging::setPirateMode(false);

  // The quip flag is inserted immediately before every '%v', including
  // patterns with multiple occurrences or none at all.
  REQUIRE(logging::insertQuipFlag("[%T] %v") == "[%T] %j%v");
  REQUIRE(logging::insertQuipFlag("%v and %v") == "%j%v and %j%v");
  REQUIRE(logging::insertQuipFlag("[%T] %l") == "[%T] %l");
}

TEST_CASE("BitOps", "[BitOps]") {
  for(int i=0; i<8; i++) {
    REQUIRE (BitOps::reverse_bits((uint8_t)(1<<i)) == (uint8_t)(1<<(7-i)));
  }
  REQUIRE (BitOps::reverse_bits((uint8_t)0x80) == (uint8_t)0x01);
  REQUIRE (BitOps::reverse_bits((uint8_t)0xa5) == (uint8_t)0xa5);
  REQUIRE (BitOps::reverse_bits((uint8_t)0x82) == (uint8_t)0x41);
  for(int i=0; i<16; i++) {
    REQUIRE (BitOps::reverse_bits((uint16_t)(1<<i)) == (uint16_t)(1<<(15-i)));
  }
  REQUIRE (BitOps::reverse_bits((uint16_t)0x82) == 0x4100);
}
