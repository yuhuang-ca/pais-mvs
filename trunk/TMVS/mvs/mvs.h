#ifndef __PAIS_MVS_H__
#define __PAIS_MVS_H__

#include "patch.h"
#include "../io/fileloader.h"

namespace PAIS {
	class Patch;

	class MVS {
	private:
		// image cell size (pixel*pixel)
		int cellSize;
		// patch radius in pixel
		int patchRadius;
		// patch size 2*radius+1
		int patchSize;
		// normalized intensity variation in patch
		double textureVariation;
		// camera container
		vector<Camera>  cameras;
		// patch container
		map<int, Patch> patches;
		// cell map container
		vector<CellMap> cellMaps;
		// pixel-wised distance weighting of patch
		Mat_<double> patchDistWeight;

		// initialize cell map using given cell size
		bool initCellMaps();
		// initial pixel-wised distance weighting of patch
		void initPatchDistanceWeighting();

		// get top priority patch id to expansion
		int getTopPriorityPatchId() const;

	public:
		// constructor
		MVS(const int cellSize = 2, const int patchRadius = 15, const double textureVariation = 36);
		~MVS(void);

		// loader
		void loadNVM(const char* fileName);

		// getter
		vector<Camera>&  getCameras() { return cameras;  }
		map<int, Patch>& getPatches() { return patches;  }
		const vector<Camera>&  getCameras()  const { return cameras;  }
		const map<int, Patch>& getPatches()  const { return patches;  }
		const vector<CellMap>& getCellMaps() const { return cellMaps; }
		const Mat_<double>& getPatchDistanceWeighting() const { return patchDistWeight; }
		const Patch& getPatch(const int id) const { return patches.at(id); }

		int    getCellSize()         const { return cellSize;         }
		int    getPatchRadius()      const { return patchRadius;      }
		int    getPatchSize()        const { return patchSize;        }
		double getTextureVariation() const { return textureVariation; }

		// processor
		bool refineSeedPatches();
	};
};

#endif