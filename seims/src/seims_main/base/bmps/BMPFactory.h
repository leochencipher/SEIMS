/*!
 * \brief Base namespace for implementation of BMP configuration
 * \author Liang-Jun Zhu
 */
#pragma once
#include "utilities.h"

#include "BMPText.h"
#include "MongoUtil.h"

/*!
 * \brief Base class of all kind of BMPs Factory.
 *        Read from BMP_SCENARIOS collection of MongoDB
 * \ingroup bmps
 */
namespace MainBMP {
/*!
 * \class BMPFactory
 * \ingroup MainBMP
 *
 * \brief Base class to initiate a BMP data
 *
 */
class BMPFactory {
public:
    /// Constructor
    BMPFactory(int scenarioId, int bmpId, int subScenario, int bmpType, int bmpPriority, string distribution,
               string parameter, string location);

    /// virtual Destructor
    virtual ~BMPFactory(void);

    /// Load BMP parameters from MongoDB
    virtual void loadBMP(mongoc_client_t *conn, string &bmpDBName) = 0;

    /// Load BMP parameters from SQLite
    ///virtual void loadBMP(string bmpDatabasePath) = 0;
    /*!  Get BMP type
       1 - reach BMPs which are attached to specific reaches and will change the character of the reach.
       2 - areal structural BMPs which are corresponding to a specific structure in the watershed and will change the character of subbasins/cells.
       3 - areal non-structure BMPs which are NOT corresponding to a specific structure in the watershed and will change the character of subbasins/cells.
       4 - point structural BMPs
     */
    int bmpType() {
        return m_bmpType;
    }

    /// Get BMP priority
    int bmpPriority() {
        return m_bmpPriority;
    }

    /// Get subScenario ID
    int GetSubScenarioId() {
        return m_subScenarioId;
    }

    /// Output
    virtual void Dump(ostream *fs) = 0;

protected:
    /// Scenario ID
    int m_scenarioId;
    /// BMP ID
    int m_bmpId;
    /// SubScenario ID within one BMP ID
    int m_subScenarioId;
    /// BMP Type
    int m_bmpType;
    /// BMP Priority
    int m_bmpPriority;
    /// Distribution of BMP
    /// Format is [distribution data type]|[distribution parameter name]
    /// in which distribution data type may be raster or array that stored in database
    string m_distribution;
    /// Collection to
    string m_bmpCollection;
    /// Define where the BMP will be applied
    string m_location;
};
}
