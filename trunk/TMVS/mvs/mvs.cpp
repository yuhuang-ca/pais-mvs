#include "mvs.h"

using namespace PAIS;

MVS* MVS::instance = NULL;

struct PatchDist {
	int id;
	double dist;
};
struct PatchNeighbor {
	int id;
	vector<int> nid;
};

bool patchDistCompare(const PatchDist &p1, const PatchDist &p2) {
	return (p1.dist < p2.dist);
}

/* constructor */

MVS& MVS::getInstance(const MvsConfig &config) {
	if (instance==NULL) {
		instance = new MVS(config);
	} else {
		delete instance;
		instance = new MVS(config);
	}
	return *instance;
}

MVS::MVS(const MvsConfig &config) {
	setConfig(config);
}

MVS::~MVS(void) {

}

/* initialize */

void MVS::setConfig(const MvsConfig &config) {
	this->cellSize           = config.cellSize;
	this->patchRadius        = config.patchRadius;
	this->minCamNum          = config.minCamNum;
	this->visibleCorrelation = config.visibleCorrelation;
	this->textureVariation   = config.textureVariation;
	this->minCorrelation     = config.minCorrelation;
	this->maxFitness         = config.maxFitness;
	this->minLOD             = config.minLOD;
	this->maxLOD             = config.maxLOD;
	this->lodRatio           = config.lodRatio;
	this->maxCellPatchNum    = config.maxCellPatchNum;
	this->reduceNormalRange  = config.reduceNormalRange;
	this->adaptiveEnable     = config.adaptiveEnable;
	this->distWeighting      = config.distWeighting;
	this->diffWeighting      = config.diffWeighting;
	this->neighborRadius     = config.neighborRadius;
	this->minRegionRatio     = config.minRegionRatio;
	this->depthRangeScalar   = config.depthRangeScalar;
	this->particleNum        = config.particleNum;
	this->maxIteration       = config.maxIteration;
	this->expansionStrategy  = config.expansionStrategy;
	this->patchSize          = (patchRadius<<1)+1;

	printConfig();

	initPatchDistanceWeighting();
}

bool MVS::initCellMaps() {
	if (cameras.empty()) {
		printf("can't initial cell maps\n");
		return false;
	}

	cellMaps.clear();

	for (int i = 0; i < cameras.size(); i++) {
		cellMaps.push_back( CellMap(cameras[i], cellSize) );
	}

	return true;
}

void MVS::initPriorityQueue() {
	queue.clear();
	map<int, Patch>::const_iterator it;
	for (it = patches.begin(); it != patches.end(); ++it) {
		queue.push_back(it->second.getId());
	}
}

void MVS::initPatchDistanceWeighting() {
	patchDistWeight = Mat_<double>(patchSize, patchSize);
	double sigma = distWeighting;
	double s2 = 1.0/(2.0*sigma*sigma);
    double s = 1.0/(2.0*M_PI*sigma*sigma);

	double e, g;
	for (int x = 0; x < patchSize; ++x) {
		for (int y = 0; y < patchSize; ++y) {
			e = -( pow((double)(x-patchRadius), 2)+ pow((double)(y-patchRadius), 2))*s2;
			g = s*exp(e);
			patchDistWeight.at<double>(x,y) = g;
		}
	}

	Scalar n = sum(patchDistWeight);
	patchDistWeight = patchDistWeight / n[0];
}

void MVS::setCellMaps() {
	initCellMaps();

	map<int, Patch>::iterator it;
	int camNum, cx, cy;
	for (it = patches.begin(); it != patches.end(); ++it) {
		Patch &pth                     = it->second;
		camNum                         = pth.getCameraNumber();
		const vector<Vec2d> &imgPoints = pth.getImagePoints();
		const vector<int> &camIdx      = pth.getCameraIndices();

		for (int i = 0; i < camNum; ++i) {
			cx = (int) (imgPoints[i][0] / cellSize);
			cy = (int) (imgPoints[i][1] / cellSize);
			cellMaps[camIdx[i]].insert(cx, cy, pth.getId());
		}
	}
}

void MVS::reCentering() {
	int count = 1;
	int num   = (int) patches.size();
	map<int, Patch>::iterator it;
	for (it = patches.begin(); it != patches.end(); ++it, ++count) {
		printf("\rre-triangulation: %d / %d", count, num);
		Patch &pth = it->second;
		pth.reCentering();
	}
	printf("\n");
}

/* io */

void MVS::loadNVM(const char* fileName) {
	FileLoader::loadNVM(fileName, *this);
	reCentering();
}

void MVS::loadNVM2(const char *fileName) {
	FileLoader::loadNVM2(fileName, *this);
	reCentering();
}

void MVS::loadMVS(const char* fileName) {
	FileLoader::loadMVS(fileName, *this);
}

void MVS::writeMVS(const char* fileName) const {
	FileWriter::writeMVS(fileName, *this);
}

void MVS::writePLY(const char *fileName) const {
	FileWriter::writePLY(fileName, *this);
}

void MVS::writePSR(const char *fileName) const {
	FileWriter::wirtePSR(fileName, *this);
}

/* main functions */

void MVS::refineSeedPatches() {
	if ( patches.empty() ) {
		printf("No seed patches\n");
		return;
	}

	map<int, Patch>::iterator it;
	for (it = patches.begin(); it != patches.end(); ) {
		Patch &pth = it->second;

		// remove patch with few visible camera
		if (pth.getCameraNumber() < minCamNum) {
			it = patches.erase(it);
			continue;
		}

		pth.refine();
		pth.removeInvisibleCamera();

		if ( !runtimeFiltering(pth) ) {
			it = patches.erase(it);
			continue;
		}

		// dispatch viewer update event
		addPatchView(pth);

		printf("ID: %d \t LOD: %d \t fit: %.2f \t pri: %.2f\n", pth.getId(), pth.getLOD(), pth.getFitness(), pth.getPriority());

		++it;
	}
	return;
}

void MVS::expansionPatches() {
	// initialize cell maps (project seed patches)
	setCellMaps();
	// initialize seed patch into priority queue
	initPriorityQueue();

	int pthId = getPatchIdFromQueue();
	int count = 0;
	while ( !queue.empty() ) {
		// get top priority seed patch
		Patch *pthP = getPatch(pthId);
		// skip if not found
		if (pthP == NULL) continue;
		Patch &pth = *pthP;

		pth.setExpanded();

		printf("parent: fit: %f \t pri: %f \t camNum: %d\n", pth.getFitness(), pth.getPriority(), pth.getCameraNumber());
		
		// skip
		if ( !runtimeFiltering(pth) ) {
			printf("Top priority patch deleted\n");
			deletePatch(pth);
			continue;
		}

		// expand patch
		expandNeighborCell(pth);
		
		if (count++ % 500 == 0) {
			writeMVS("auto_save.mvs");
		}

		// get next seed patch id
		pthId = getPatchIdFromQueue();
	}
}

void MVS::patchQuantization(const int thetaNum, const int phiNum, const int distNum) {
	// normal bound in spherical coordinate
	double minTheta =  DBL_MAX;
	double maxTheta = -DBL_MAX;
	double minPhi   =  DBL_MAX;
	double maxPhi   = -DBL_MAX;
	// plane distance bound to origin
	double minDist  =  DBL_MAX;
	double maxDist  = -DBL_MAX;

	// get bound
	double dist;
	for (map<int, Patch>::iterator it = patches.begin(); it != patches.end(); ++it) {
		const Patch &pth = it->second;
		const Vec2d &normalS = pth.getSphericalNormal();
		dist = -pth.getNormal().ddot(pth.getCenter());

		if (dist < minDist) {
			minDist = dist;
		}
		if (dist > maxDist) {
			maxDist = dist;
		}
		if (normalS[0] < minTheta) {
			minTheta = normalS[0];
		}
		if (normalS[0] > maxTheta) {
			maxTheta = normalS[0];
		}
		if (normalS[1] < minPhi) {
			minPhi = normalS[1];
		}
		if (normalS[1] > maxPhi) {
			maxPhi = normalS[1];
		}
	}

	// set container range
	const double thetaRange = (maxTheta - minTheta);
	const double phiRange   = (maxPhi   - minPhi  );
	const double distRange  = (maxDist  - minDist );

	const double thetaStep = thetaRange / thetaNum;
	const double phiStep   = phiRange   / phiNum;
	const double distStep  = distRange  / distNum;

	vector<vector<vector< vector<int> > > > bins(thetaNum, vector<vector<vector<int> > >(phiNum, vector<vector<int > >(distNum, vector<int>(0)) ));

	// vote in hough space
	double thetaN, phiN, distN;
	int thetaIdx, phiIdx, distIdx;
	double quanTheta, quanPhi, quanDist;
	
	for (map<int, Patch>::iterator it = patches.begin(); it != patches.end(); ++it) {
		const Patch &pth = it->second;
		const Vec2d &normalS = pth.getSphericalNormal();
		const Vec3d &center  = pth.getCenter();
		const Vec3d &normal  = pth.getNormal();
		dist = -normal.ddot(center);

		// normalized to [0, 1]
		thetaN = (normalS[0] - minTheta) / thetaRange;
		phiN   = (normalS[1] - minPhi)   / phiRange;
		distN  = (dist       - minDist)  / distRange;

		// get index
		thetaIdx = cvRound(thetaN * (thetaNum-1));
		phiIdx   = cvRound(phiN   * (phiNum  -1));
		distIdx  = cvRound(distN  * (distNum -1));
		
		bins[thetaIdx][phiIdx][distIdx].push_back(pth.getId());
	}
	
	// patch quantization
	double d;
	Vec3d quanNormal, quanCenter, projCenter;
	int pthNum;
	for (int thetaIdx = 0; thetaIdx < thetaNum; ++thetaIdx) {
		for (int phiIdx = 0; phiIdx < phiNum; ++phiIdx) {
			for (int distIdx = 0; distIdx < distNum; ++distIdx) {
				quanTheta = thetaIdx * thetaStep + minTheta;
				quanPhi   = phiIdx   * phiStep   + minPhi;
				quanDist  = distIdx  * distStep  + minDist;
				Utility::spherical2Normal(Vec2d(quanTheta, quanPhi), quanNormal);

				const vector<int> &bin = bins[thetaIdx][phiIdx][distIdx];
				pthNum = (int) bin.size();
				for (int i = 0; i < pthNum; ++i) {
					Patch *pthP = getPatch(bin[i]);
					if (pthP == NULL) continue;
					Patch &pth = *pthP;
					const Vec3d &center = pth.getCenter();
					d = (center + quanDist*quanNormal).ddot(quanNormal);
					// on plane center
					projCenter = center - d*quanNormal;
					pth.setQuantization(center, quanNormal);
				}
			}
		}
	}
}

void MVS::cellFiltering() {
	const int camNum = (int) cameras.size();
	int mapWidth, mapHeight, pthNum;
	double corrSum;

	for (int i = 0; i < camNum; ++i) {
		CellMap &map = cellMaps[i];
		mapWidth     = map.getWidth();
		mapHeight    = map.getHeight(); 
		
		for (int x = 0; x < mapWidth; ++x) {
			for (int y = 0; y < mapHeight; ++y) {
				const vector<int> &cell = map.getCell(x, y);
				pthNum            = (int) cell.size();
				// patch index to be removed
				vector<int> removeIdx;

				for (int j = 0; j < pthNum; ++j) {
					corrSum = 0;
					for (int k = 0; k < pthNum; ++k) {
						if (j == k) continue;
						const Patch *pthP = getPatch(cell[k]);
						if (pthP == NULL) continue;
						const Patch &pth = *pthP;
						corrSum += pth.getCorrelation();
					}
					const Patch *pthP = getPatch(cell[j]);
					if (pthP == NULL) continue;
					const Patch &pth = *pthP;
					if (pth.getCorrelation() * pth.getCameraNumber() < corrSum) {
						removeIdx.push_back(cell[j]);
					}
				}

				for (int j = 0; j < (int) removeIdx.size(); ++j) {
					deletePatch(removeIdx[j]);
				}
			}
		}
	}
}

void MVS::neighborCellFiltering(const double neighborRatio) {
	const int camNum = (int) cameras.size();
	int mapWidth, mapHeight;
	for (int i = 0; i < camNum; ++i) {
		CellMap &map = cellMaps[i];
		mapWidth     = map.getWidth();
		mapHeight    = map.getHeight(); 

		for (int x = 0; x < mapWidth; ++x) {
			for (int y = 0; y < mapHeight; ++y) {
				// center cell
				const vector<int> &cell = map.getCell(x, y);
				vector<int> removeIdx;

				// neighbor cells
				int nx [] = {x, x-1, x+1, x-1, x+1, x+1, x  , x-1, x  };
				int ny [] = {y, y-1, y-1, y+1, y+1, y  , y+1, y  , y-1};

				const int pthNum = (int) cell.size();

				// center cell
				for (int j = 0; j < pthNum; ++j) {
					// center patch
					const Patch *centerPthP = getPatch(cell[j]);
					if (centerPthP == NULL) continue;
					const Patch &centerPth = *centerPthP;

					int neighborPthSum = 0;
					int neighborPthNum = 0;

					// neighbor cell
					for (int j = 0; j < 9; ++j) {
						// skip out of boundary
						if ( !map.inMap(nx[j], ny[j]) ) continue;

						const vector<int> &neighborCell = map.getCell(nx[j], ny[j]);
						int neighborCellPthNum = (int) neighborCell.size();
						neighborPthSum += neighborCellPthNum;

						for (int k = 0; k < neighborCellPthNum; ++k) {
							const Patch *neighborPthP = getPatch(neighborCell[k]);
							if (neighborPthP == NULL) continue;
							const Patch &neighborPth = *neighborPthP;

							if ( Patch::isNeighbor(centerPth, neighborPth) ) {
								++neighborPthNum;
							}
						} // end of neighbor patch
					} // end of neighbor cell

					// mark as remove
					if ((double) neighborPthNum / (double) neighborPthSum < neighborRatio) {
						removeIdx.push_back(centerPth.getId());
					}
				} // end of center cell

				// remove patch
				for (int i = 0; i < (int) removeIdx.size(); ++i) {
					deletePatch(removeIdx[i]);
				}

			} // end of map y
		} // end of map x
		
	}
}

void MVS::visibilityFiltering() {
	map<int, Patch>::iterator it;
	int camNum, cx, cy;
	double depth, neighborDepth;
	for (it = patches.begin(); it != patches.end(); ) {
		Patch &pth = it->second;
		camNum = pth.getCameraNumber();
		const vector<Vec2d> &imgPoints = pth.getImagePoints();
		const vector<int> &camIdx = pth.getCameraIndices();
		
		// count visible views
		int visibleCount = camNum;
		for (int i = 0; i < camNum; ++i) {
			const Camera &cam = cameras[camIdx[i]];
			depth = norm(pth.getCenter() - cam.getCenter());
			cx = (int) (imgPoints[i][0] / cellSize);
			cy = (int) (imgPoints[i][1] / cellSize);
			const vector<int> &cell = cellMaps[camIdx[i]].getCell(cx, cy);

			// number of patches in cell
			const int pthNum = (int) cell.size();
			for (int p = 0; p < pthNum; ++p) {
				if (cell[p] == pth.getId()) continue;
				const Patch *pthP = getPatch(cell[p]);
				if (pthP == NULL) continue;
				const Patch &pth = *pthP;
				neighborDepth = norm(pth.getCenter() - cam.getCenter());
				if (depth > neighborDepth) {
					--visibleCount;
					break;
				}
			}
		}

		// drop patch if few visible camera
		if (visibleCount < minCamNum) {
			it = deletePatch(pth);
			continue;
		}

		++it;
	}
}

void MVS::neighborPatchFiltering() {
	// copy patch id
	vector<int> patchIds;
	for (map<int, Patch>::const_iterator it = patches.begin(); it != patches.end(); ++it) {
		const Patch &pth = it->second; // current patch
		patchIds.push_back(pth.getId());
	}

	// patch neighbor information
	vector<PatchNeighbor> neighbor;

	// check k nearest neighbor of each patch
	int count = 1;
	#pragma omp parallel for
	for (int i = 0; i < (int) patchIds.size(); ++i) {
		#pragma omp critical
		{
			printf("\rfiltering: %d / %d", count++, patches.size());
		}

		const Patch *pthP = getPatch(patchIds[i]); // current patch
		if (pthP == NULL) continue;
		const Patch &pth = *pthP;

		vector<PatchDist> dist;              // dist container
		for (map<int, Patch>::const_iterator itN = patches.begin(); itN != patches.end(); ++itN) {
			const Patch &pthN = itN->second; // current neighbor patch

			// skip self
			if (pthN.getId() == pth.getId()) continue;

			// distance object
			PatchDist pthDist;
			pthDist.id   = pthN.getId();
			pthDist.dist = norm(pth.getCenter() - pthN.getCenter());

			dist.push_back(pthDist);
		}

		// sort by distance
		sort(dist.begin(), dist.end(), patchDistCompare);

		// get local neighbor information
		PatchNeighbor pn;
		pn.id = pth.getId();
		for (int i = 0; i < (int) dist.size(); ++i) {
			const Patch *pthNP = getPatch(dist[i].id);
			if (pthNP == NULL) continue;
			const Patch &pthN = *pthNP;
			if (dist[i].dist > neighborRadius) break;
			pn.nid.push_back(pthN.getId());
		}
		#pragma omp critical
		{
			neighbor.push_back(pn);
		}
	}

	// get average neighbor number
	int avgNeighborNum = 0;
	for (int i = 0; i < (int) neighbor.size(); ++i) {
		avgNeighborNum += (int) neighbor[i].nid.size();
	}
	avgNeighborNum /= (int) neighbor.size();
	printf("average neighbor number: %d\n", avgNeighborNum);

	// remove outlier patches which neighbor number < (average neighbor number*0.25)
	for (int i = 0; i < (int) neighbor.size(); ++i) {
		if ((int) neighbor[i].nid.size() < (avgNeighborNum>>2) ) {
			deletePatch(neighbor[i].id);
		}
	}
}

/* process */

void MVS::expandNeighborCell(const Patch &pth) {
	const int camNum               = pth.getCameraNumber();
	const vector<int> &camIdx      = pth.getCameraIndices();
	const vector<Vec2d> &imgPoints = pth.getImagePoints();

	int cx, cy;
	for (int i = 0; i < camNum; ++i) {
		// only expansion visible image cell
		//if (camIdx[i] != pth.getReferenceCameraIndex()) continue;

		// camera
		const Camera &cam = cameras[camIdx[i]];
		// cell map
		const CellMap &map = cellMaps[camIdx[i]];

		// position on cell map
		cx = (int) (imgPoints[i][0] / cellSize);
		cy = (int) (imgPoints[i][1] / cellSize);

		// check neighbor cells
		int nx [] = {cx-1, cx  , cx+1, cx  };
		int ny [] = {cy  , cy-1, cy  , cy+1};

		for (int j = 0; j < 4; ++j) {
			// skip out of map
			if ( !map.inMap(nx[j], ny[j]) ) continue;

			// skip neighbor cell with exist neighbor patch or discontinuous
			const vector<int> &cell = map.getCell(nx[j], ny[j]);
			if ( skipNeighborCell(cell, pth) ) continue;

			// expand neighbor cell (create expansion patch)
			expandCell(cam, pth, nx[j], ny[j]);
		} // end of neighbor cell
	} // end of cameras
}

void MVS::expandCell(const PAIS::Camera &cam, const Patch &parent, const int cx, const int cy) {
	// get expansion patch center
	Vec3d center;
	getExpansionPatchCenter(cam, parent, cx, cy, center);

	// get expansion patch
	Patch expPatch(center, parent);
	expPatch.refine();
	expPatch.removeInvisibleCamera();

	insertPatch(expPatch);
}

void MVS::insertPatch(const Patch &pth) {
	if ( !runtimeFiltering(pth) ) return;

	const int camNum = pth.getCameraNumber();
	const vector<Vec2d> &imgPoints = pth.getImagePoints();
	const vector<int>   &camIdx    = pth.getCameraIndices();
	int cx, cy;

	// insert into patches container
	patches.insert(pair<int, Patch>(pth.getId(), pth));
	// insert into priority queue
	queue.push_back(pth.getId());
	
	// insert into cell maps
	for (int i = 0; i < camNum; ++i) {
		cx = (int) (imgPoints[i][0] / cellSize);
		cy = (int) (imgPoints[i][1] / cellSize);
		cellMaps[camIdx[i]].insert(cx, cy, pth.getId());
	}

	// dispatch viewer update event
	addPatchView(pth);
}

map<int, Patch>::iterator MVS::deletePatch(Patch &pth) {
	return deletePatch(pth.getId());
}

map<int, Patch>::iterator MVS::deletePatch(const int id) {
	map<int, Patch>::iterator it = patches.find(id);
	if (it == patches.end()) return patches.end();

	if(!cellMaps.empty()) {
		const Patch &pth = it->second;
		const int camNum = pth.getCameraNumber();
		const vector<int> &camIdx = pth.getCameraIndices();
		const vector<Vec2d> &imgPoints = pth.getImagePoints();

		int cx, cy;
		for (int i = 0; i < camNum; ++i) {
			cx = (int) (imgPoints[i][0] / cellSize);
			cy = (int) (imgPoints[i][1] / cellSize);
			cellMaps[camIdx[i]].drop(cx, cy, pth.getId());
		}
	}

	return patches.erase(it);
}

int MVS::getPatchIdFromQueue() const {
	int id = -1;

	switch (expansionStrategy) {
	default:
	case EXPANSION_BEST_FIRST:
		id = getTopPriorityPatchId();
		break;
	case EXPANSION_WORST_FIRST:
		id = getLastPriorityPatchId();
		break;
	case EXPANSION_BREATH_FIRST:
		id = getBreathFirstPatchId();
		break;
	case EXPANSION_DEPTH_FIRST:
		id = getDepthFirstPatchId();
		break;
	}

	printf("queue %d patches %d\n", queue.size(), patches.size());

	return id;
}

int MVS::getTopPriorityPatchId() const {
	vector<int>::iterator it;
	vector<int>::iterator topIt = queue.end();
	double topPriority = DBL_MAX;

	for (it = queue.begin(); it != queue.end();) { 
		const Patch *pthP = getPatch(*it);
		if (pthP == NULL) {
			it = queue.erase(it);
			continue;
		}
		const Patch &pth = *pthP;

		// skip expanded
		if ( pth.isExpanded() ) {
			it = queue.erase(it);
			continue;
		}

		// update top priority
		if (pth.getPriority() < topPriority) {
			topPriority = pth.getPriority();
			topIt       = it;
		}

		++it;
	}

	int topId = -1;

	// delete top id from queue
	if (topIt != queue.end()) {
		topId = *topIt;
		queue.erase(topIt);
	}

	return topId;
}

int MVS::getLastPriorityPatchId() const {
	vector<int>::iterator it;
	vector<int>::iterator topIt = queue.end();
	double topPriority = -DBL_MAX;

	for (it = queue.begin(); it != queue.end();) { 
		const Patch *pthP = getPatch(*it);
		if (pthP == NULL) {
			it = queue.erase(it);
			continue;
		}
		const Patch &pth = *pthP;

		// skip expanded
		if ( pth.isExpanded() ) {
			it = queue.erase(it);
			continue;
		}

		// update top priority
		if (pth.getPriority() > topPriority) {
			topPriority = pth.getPriority();
			topIt       = it;
		}

		++it;
	}

	int topId = -1;

	// delete top id from queue
	if (topIt != queue.end()) {
		topId = *topIt;
		queue.erase(topIt);
	}

	return topId;
}

int MVS::getBreathFirstPatchId() const {
	vector<int>::iterator it;
	int topId = -1;

	for (it = queue.begin(); it != queue.end();) {
		const Patch *pthP = getPatch(*it);
		if (pthP == NULL) {
			it = queue.erase(it);
			continue;
		}
		const Patch &pth = *pthP;

		// skip expanded
		if ( pth.isExpanded() ) {
			it = queue.erase(it);
			continue;
		}

		topId = *it;
		break;
	}

	queue.erase(it);

	return topId;
}

int MVS::getDepthFirstPatchId() const {
	vector<int>::iterator it;
	int topId = -1;

	for (it = queue.end()-1; it != queue.begin();) {
		const Patch *pthP = getPatch(*it);
		if (pthP == NULL) {
			queue.erase(it);
			it = queue.end()-1;
			continue;
		}
		const Patch &pth = *pthP;

		// skip expanded
		if ( pth.isExpanded() ) {
			queue.erase(it);
			it = queue.end()-1;
			continue;
		}

		topId = *it;
		break;
	}

	queue.erase(it);

	return topId;
}

/* const function */

bool MVS::skipNeighborCell(const vector<int> &cell, const Patch &refPth) const {
	const int pthNum = (int) cell.size();
	// skip if full cell
	if (pthNum >= maxCellPatchNum) return true;

	for (int k = 0; k < pthNum; k++) {
		const Patch *pthP = getPatch(cell[k]);
		if (pthP == NULL) continue;
		const Patch &pth = *pthP;
		// skip if has robust neighbor (but depth discontinuous)
		if ( pth.getCorrelation() > minCorrelation ) return true;
		// skip if has near neighbor
		if ( Patch::isNeighbor(refPth, pth) )        return true;
	}
	return false;
}

void MVS::getExpansionPatchCenter(const PAIS::Camera &cam, const Patch &parent, const int cx, const int cy, Vec3d &center) const {
	const Vec2d &focal        = cam.getFocalLength();
	const Vec2d &principle    = cam.getPrinciplePoint();
	const Vec3d &camCenter    = cam.getCenter();
	const Vec3d &parentNormal = parent.getNormal();
	const Vec3d &parentCenter = parent.getCenter();

	// get center pixel position of cell on reference image
	const double px = (cx+0.5)*cellSize;
	const double py = (cy+0.5)*cellSize;

	// get center pixel position of cell in world coordinate
	Mat p3d(3, 1, CV_64FC1);
	p3d.at<double>(0, 0) = (px - principle[0]) / focal[0];
	p3d.at<double>(1, 0) = (py - principle[1]) / focal[1];
	p3d.at<double>(2, 0) = 1.0;
	p3d = cam.getRotation().t() * (p3d - cam.getTranslation());

	// get intersection point on patch plane
	const Vec3d v13 = parentCenter - camCenter;
	const Vec3d v12(p3d.at<double>(0, 0) - camCenter[0], 
			        p3d.at<double>(1, 0) - camCenter[1],
			        p3d.at<double>(2, 0) - camCenter[2]);
	const double u = parentNormal.ddot(v13) / parentNormal.ddot(v12);

	// expansion patch information
	center = camCenter + u*v12;
}

bool MVS::runtimeFiltering(const Patch &pth) const {
	if (pth.isDropped())                       return false;
	if (pth.getCameraNumber() < minCamNum)     return false;
	if (pth.getFitness() > maxFitness)         return false;
	if (pth.getFitness() == 0.0)               return false;
	if (pth.getPriority() > 10000)             return false;
	if (_isnan(pth.getFitness()))              return false;
	if (_isnan(pth.getPriority()))             return false;
	if (_isnan(pth.getCorrelation()))          return false;
	if (pth.getCorrelation() < minCorrelation) return false;

	// skip background
	Vec2d pt;
	for (int i = 0; i < cameras.size(); i++) {
		const Camera &cam = cameras[i];
		const Mat_<uchar> &img = cam.getPyramidImage(0);
		// out of image bound
		if ( !cam.project(pth.getCenter(), pt) ) {
			return false;
		}

		// in background
		if (img.at<uchar>(cvRound(pt[1]), cvRound(pt[0])) == 0) {
			return false;
		}
	}

	const int camNum = pth.getCameraNumber();

	// skip invisible cameras 
	int count = 0;
	for (int i = 0; i < camNum; ++i) {
		const Camera &cam = getCamera(pth.getCameraIndices()[i]);
		if (pth.getNormal().ddot(-cam.getOpticalNormal()) > 0) {
			count++;
		}
	}
	if (count < minCamNum) return false;

	// cell patch number filtering
	if (cellMaps.empty()) return true; // skip if not set cell maps (during seed patch refinement)
	const vector<Vec2d> &imgPoints = pth.getImagePoints();
	const vector<int>   &camIdx    = pth.getCameraIndices();
	int cx, cy;
	int fullCellCounter = 0;
	for (int i = 0; i < camNum; ++i) {
		cx = (int) (imgPoints[i][0] / cellSize);
		cy = (int) (imgPoints[i][1] / cellSize);
		const vector<int> &cell = cellMaps[camIdx[i]].getCell(cx, cy);
		// find this patch in cell
		vector<int>::const_iterator it = find(cell.begin(), cell.end(), pth.getId());
		if (it != cell.end()) return true;
		// cell is full and not contain this patch
		if ( cell.size() >= maxCellPatchNum && it == cell.end()) {
			++fullCellCounter;
		}
	}
	if (fullCellCounter >= camNum) return false;

	return true;
}

void MVS::printConfig() const {
	printf("MVS config\n");
	printf("-------------------------------\n");
	printf("cell size:\t%d pixel\n", cellSize);
	printf("patch radius:\t%d pixel\n", patchRadius);
	printf("patch size:\t%d pixel\n", patchSize);
	printf("minimum camera number:\t%d\n", minCamNum);
	printf("texture variation:\t%f\n", textureVariation);
	printf("visible correlation:\t%f\n", visibleCorrelation);
	printf("minimum correlation:\t%f\n", minCorrelation);
	printf("maximum fitness:\t%f\n", maxFitness);
	printf("LOD ratio:\t%f\n", lodRatio);
	printf("minimum LOD:\t%d\n", minLOD);
	printf("maximum LOD:\t%d\n", maxLOD);
	printf("maximum cell patch number:\t%d patch/cell\n", maxCellPatchNum);
	printf("reduce normal search range:\tpi/%f\n", reduceNormalRange);
	if (adaptiveEnable) {
		printf("adaptive enable:\tenable\n");
	} else {
		printf("adaptive enable:\tdisable\n");
	}
	printf("distance weighting:\t%f\n", distWeighting);
	printf("difference weighting:\t%f\n", diffWeighting);
	printf("neighbor radius:\t%f\n", neighborRadius);
	printf("minimum region ratio:\t%f\n", minRegionRatio);
	printf("depth range scalar:\t%f\n", depthRangeScalar);
	printf("particle number:\t%d\n", particleNum);
	printf("maximum iteration number:\t%d\n", maxIteration);
	switch (expansionStrategy) {
	default:
	case EXPANSION_BEST_FIRST:
		printf("expansion strategy:\tBest first\n");
		break;
	case EXPANSION_WORST_FIRST:
		printf("expansion strategy:\tWorst first\n");
		break;
	case EXPANSION_BREATH_FIRST:
		printf("expansion strategy:\tBreath first\n");
		break;
	case EXPANSION_DEPTH_FIRST:
		printf("expansion strategy:\tDepth first\n");
		break;
	}
	printf("-------------------------------\n");
}

/* getter */
const Patch* MVS::getPatch(const int id) const {
	if ( patches.find(id) != patches.end() ) {
		return &patches.at(id);
	} else {
		return NULL;
	}
}

Patch* MVS::getPatch(const int id) {
	if ( patches.find(id) != patches.end() ) {
		return &patches.at(id);
	} else {
		return NULL;
	}
}