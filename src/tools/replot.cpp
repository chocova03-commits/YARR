#include <string>
#include <iostream>
#include "logging.h"
#include "LoggingConfig.h"

#include "Histo2d.h"
#include "Histo1d.h"
#include "Histo3d.h"
#include "GraphErrors.h"
#include "LoadJson.h"

int main(int argc, char*argv[]) {
	// Setup logger with some defaults
	logging::setupLoggers(logging::defaultConfig());

	if (argc < 2 || argc > 2) {
		std::cout << "Usage: " << argv[0] << " <filename>" << std::endl;
		return -1;
	}

        auto file_name = std::string(argv[1]);

        auto j = JsonHelper::openJsonFile(file_name);

        if(j.contains("Type")) {
		std::string h_type = j["Type"];
		if(h_type == "Histo1d") {
			Histo1d h1("Temp1", 1, 0.0, 1.0);
			if(!h1.fromJson(j)) {
				std::cout << "ABORTING: Failed to read Histo1d\n";
				return 1;
			}
			h1.plot("replot", "./");
		} else if(h_type == "Histo2d") {
			Histo2d h2("Temp2", 1, 0.0, 1.0, 1, 0.0, 1.0);
			if (!h2.fromJson(j)) {
				std::cout << "ABORTING: Failed to read Histo2d\n";
				return 1;
			}

			h2.plot("replot", "./");
		} else if(h_type == "Histo3d") {
			Histo3d h3("Temp3", 1, 0.0, 1.0, 1, 0.0, 1.0, 1, 0.0, 1.0);
			if (!h3.fromJson(j)) {
				std::cout << "ABORTING: Failed to read Histo3d\n";
				return 1;
			}
			h3.plot("replot", "./");
		} else if(h_type == "GraphErrors") {
			GraphErrors ge("TempErr");
			if (!ge.fromJson(j)) {
				std::cout << "ABORTING: Failed to read GraphErrors\n";
				return 1;
			}
			ge.plot("replot", "./");
		} else {
			std::cout << "ABORTING: Histogram type " << h_type << " not recognised for replotting\n";
		}
        } else {
		std::cout << "ABORTING: Failed to identify histogram type for replotting\n";
	}
	return 0;
}