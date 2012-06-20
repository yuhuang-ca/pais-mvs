#include "stdafx.h"

#include <time.h>

#include <opencv2\opencv.hpp>

#include "mvs\mvs.h"
#include "view\mvsviewer.h"

using namespace cv;
using namespace PAIS;

int main(int argc, char* argv[])
{
	// MVS configures
	MvsConfig config;
	config.cellSize           = 4;
	config.patchRadius        = 15;
	config.distWeighting      = config.patchRadius / 3.0;
	config.diffWeighting      = 128*128;
	config.minCamNum          = 3;
	config.textureVariation   = 1000;
	config.visibleCorrelation = 0.87;
	config.minCorrelation     = 0.99;
	config.minLOD             = 0;
	config.maxCellPatchNum    = 5;
	config.particleNum        = 15;
	config.maxIteration       = 60;

	// set MVS instance
	MVS &mvs = MVS::getInstance(config);

	// LOAD MVS file
	//mvs.loadNVM("../../../TMVS_data/wc/wc.nvm");
	//mvs.loadNVM((char*)argv[1]);
	mvs.loadMVS((char*)argv[1]);
	//mvs.loadMVS("exp.mvs");
	printf("patches: %d\n", mvs.getPatches().size());
	
	// start MVS process
	clock_t start_t, end_t;
	start_t = clock();
	mvs.refineSeedPatches();
	mvs.writeMVS("seed.mvs");
	mvs.expansionPatches();
	mvs.writeMVS("exp.mvs");
	mvs.setCellMaps();
	mvs.cellFiltering();
	printf("patches: %d\n", mvs.getPatches().size());
	mvs.visibilityFiltering();
	printf("patches: %d\n", mvs.getPatches().size());
	mvs.neighborCellFiltering(0.1, 0.25);
	printf("patches: %d\n", mvs.getPatches().size());
	mvs.writeMVS("filter.mvs");
	end_t = clock();
	printf("time1\t%f\n", (double)(end_t - start_t) / CLOCKS_PER_SEC);

	// show last result
	MvsViewer viewer(mvs, true);

	return 0;
}