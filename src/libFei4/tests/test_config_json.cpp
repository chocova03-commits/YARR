#include "catch.hpp"
#include <sstream>

#include "AllChips.h"
#include "FrontEndCfg.h"

// Check that loadConfig(writeConfig(x)) reproduces the same json, i.e.
// that a freshly constructed config round-trips through json without loss.
TEST_CASE("Fei4JsonBounce", "[fei4][json]") {
  auto fe = StdDict::getFrontEnd("FEI4B");
  auto fecfg = dynamic_cast<FrontEndCfg*>(&*fe);
  REQUIRE(fecfg);

  json written;
  fecfg->writeConfig(written);

  std::stringstream writtenj;
  writtenj << written;

  auto fe2 = StdDict::getFrontEnd("FEI4B");
  auto fecfg2 = dynamic_cast<FrontEndCfg*>(&*fe2);
  REQUIRE(fecfg2);

  fecfg2->loadConfig(written);

  json bounced;
  fecfg2->writeConfig(bounced);

  std::stringstream bouncedj;
  bouncedj << bounced;

  REQUIRE(writtenj.str() == bouncedj.str());
}
