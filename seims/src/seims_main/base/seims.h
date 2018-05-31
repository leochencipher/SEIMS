/*!
 * \brief The SEIMS related definitions and utilities header.
 * \author Liang-Jun Zhu
 * \date 2017-3-22
 */
#ifndef SEIMS_HEADER
#define SEIMS_HEADER

#include "data_raster.h"

/*!
 * \enum LayeringMethod
 * \brief Grid layering method for routing and parallel computing
 */
enum LayeringMethod {
    UP_DOWN, ///< layering-from-source method
    DOWN_UP ///< layering-from-outlet method
};

enum FlowDirectionMethod {
    TauDEM = 0,
    ArcGIS = 1
};

/*!
 *\brief Whether diagonal counter clockwise from east
 *       the first element is set to 0, for indexing convenient.
 *          1  0  1
 *          0     0
 *          1  0  1
 *       e.g. the corresponding D8 flow direction of TauDEM rule:
 *          4  3  2
 *          5     1
 *          6  7  8
 */
const int DiagonalCCW[9] = {0, 0, 1, 0, 1, 0, 1, 0, 1};
/*!
 * \brief Common used const.
 */
const float _1div3 = 0.3333333333333333f; /// 1. / 3.
const float _2div3 = 0.6666666666666666f; /// 2. / 3.
const float _8div3 = 2.6666666666666665f; /// 8. / 3.
const float SQ2 = 1.4142135623730951f;
const float deg2rad = 0.017453292519943295f; /// PI / 180.
const float rad2deg = 57.29577951308232f; /// 180. / PI

#define MIN_FLUX        1e-12f /// \def minimum flux (m3/s) in kinematic wave
#define MAX_ITERS_KW    10     /// \def maximum iterate number in kinematic wave method
#define MIN_SLOPE       1e-6f  /// \def minimum slope (tan value)

#define IntRaster       ccgl::data_raster::clsRasterData<int>
#define FloatRaster     ccgl::data_raster::clsRasterData<float>

#endif /* SEIMS_HEADER */