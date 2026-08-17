#include "catch.hpp"

#include "GraphErrors.h"

// For a given graph:
// 1. Save to json
// 2. Read into a fresh graph from that json
// 3. Check basic properties match
// 4. Re-serialize and check the json is identical (round-trip closure)
void testSaveLoad(const GraphErrors &g) {
  json j;
  g.toJson(j);

  GraphErrors out_graph{j["Name"], 0};
  bool ok = out_graph.fromJson(j);
  REQUIRE(ok);

  json j2;
  out_graph.toJson(j2);

  std::string json_output = j.dump();
  CAPTURE (json_output);

  CHECK (out_graph.size() == g.size());
  CHECK (j.dump() == j2.dump());
}

TEST_CASE("GraphErrorsOK", "[GraphErrors]") {
  SECTION("Empty") {
    GraphErrors graph("TestGraph", 0);
    testSaveLoad(graph);
  }

  SECTION("Points, no errors") {
    GraphErrors graph("TestGraph", 3);
    graph.addPoint(1.0, 2.0);
    graph.addPoint(2.0, 4.0);
    graph.addPoint(3.0, 6.0);
    testSaveLoad(graph);
  }

  SECTION("Points with x and y errors") {
    GraphErrors graph("TestGraph", 3);
    graph.addPoint(1.0, 2.0, 0.1, 0.2);
    graph.addPoint(2.0, 4.0, 0.1, 0.2);
    graph.addPoint(3.0, 6.0, 0.1, 0.2);
    testSaveLoad(graph);
  }

  SECTION("Points with only y errors") {
    GraphErrors graph("TestGraph",
                       std::vector<double>{1.0, 2.0, 3.0},
                       std::vector<double>{2.0, 4.0, 6.0},
                       std::vector<double>{},
                       std::vector<double>{0.2, 0.2, 0.2});
    testSaveLoad(graph);
  }
}

TEST_CASE("BenchmarkGraphErrors", "[!benchmark]") {
  GraphErrors graph("BENCH", 100);
  for (int i=0; i<100; i++) {
    graph.addPoint(i, random(), 0.1, 0.1);
  }

  BENCHMARK ("ToJson") {
    json j;
    graph.toJson(j);
    return j;
  };

  json jdata;
  graph.toJson(jdata);

  BENCHMARK ("FromJson") {
    GraphErrors out("OUT", 100);
    out.fromJson(jdata);
    return out;
  };
}
