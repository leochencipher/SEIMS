#include "DataCenterMongoDB.h"

const int MAIN_DB_TABS_REQ_NUM = 6;
const char *MAIN_DB_TABS_REQ[] = {DB_TAB_FILE_IN, DB_TAB_FILE_OUT, DB_TAB_SITELIST,
                                  DB_TAB_PARAMETERS, DB_TAB_REACH, DB_TAB_SPATIAL};

const int METEO_VARS_NUM = 6;
const char *METEO_VARS[] = {DataType_MeanTemperature, DataType_MaximumTemperature,
                            DataType_MinimumTemperature, DataType_SolarRadiation,
                            DataType_WindSpeed, DataType_RelativeAirMoisture};

const int SOILWATER_VARS_NUM = 5;
const char *SOILWATER_VARS[] = {VAR_SOL_WPMM, VAR_SOL_AWC, VAR_SOL_UL,
                                VAR_SOL_SUMAWC, VAR_SOL_SUMSAT};

DataCenterMongoDB::DataCenterMongoDB(InputArgs* input_args, MongoClient *client, ModuleFactory* factory,
                                     int subBasinID /* = 0 */) :
    m_mongodbIP(input_args->m_host_ip), m_mongodbPort(input_args->m_port), m_mongoClient(client),
    m_mainDatabase(nullptr), m_spatialGridFS(nullptr),
    m_climDBName(""), m_scenDBName(""),
    DataCenter(input_args, factory, subBasinID) {
    m_spatialGridFS = new MongoGridFS(m_mongoClient->getGridFS(m_modelName, DB_TAB_SPATIAL));
    if (getFileInStringVector()) {
        m_input = SettingsInput::Init(m_fileIn1DStrs);
        if (nullptr == m_input) {
            throw ModelException("DataCenterMongoDB", "Constructor", "Failed to initialize m_input!");
        }
    } else {
        throw ModelException("DataCenterMongoDB", "Constructor", "Failed to query FILE_IN!");
    }
    if (!getSubbasinNumberAndOutletID()) {
        throw ModelException("DataCenterMongoDB", "Constructor", "Query subbasin number and outlet ID failed!");
    }
    if (getFileOutVector()) {
        m_output = SettingsOutput::Init(m_nSubbasins, m_outletID, m_subbasinID, m_OriginOutItems);
        if (nullptr == m_output) {
            throw ModelException("DataCenterMongoDB", "Constructor", "Failed to initialize m_output!");
        }
    } else {
        throw ModelException("DataCenterMongoDB", "Constructor", "Failed to query FILE_OUT!");
    }
    /// Check the existence of all required and optional data
    if (!checkModelPreparedData()) {
        throw ModelException("DataCenterMongoDB", "checkModelPreparedData", "Model data has not been set up!");
    }
}

DataCenterMongoDB::~DataCenterMongoDB() {
    StatusMessage("Release DataCenterMongoDB...");
    if (m_spatialGridFS != nullptr) {
        delete m_spatialGridFS;
        m_spatialGridFS = nullptr;
    }
    if (m_mainDatabase != nullptr) {
        delete m_mainDatabase;
        m_mainDatabase = nullptr;
    }
}

bool DataCenterMongoDB::checkModelPreparedData() {
    /// 1. Check and get the main model database
    vector<string> existedDBNames;
    m_mongoClient->getDatabaseNames(existedDBNames);
    if (!ValueInVector(string(m_modelName), existedDBNames)) {
        cout << "ERROR: The main model is not existed: " << m_modelName << endl;
        return false;
    }
    m_mainDatabase = new MongoDatabase(m_mongoClient->getDatabase(m_modelName));
    /// 2. Check the existence of FILE_IN, FILE_OUT, PARAMETERS, REACHES, SITELIST, SPATIAL, etc
    vector<string> existedMainDBTabs;
    m_mainDatabase->getCollectionNames(existedMainDBTabs);
    for (int i = 0; i < MAIN_DB_TABS_REQ_NUM; ++i) {
        if (!ValueInVector(string(MAIN_DB_TABS_REQ[i]), existedMainDBTabs)) {
            cout << "ERROR: Table " << MAIN_DB_TABS_REQ[i] << " must be existed in " << m_modelName << endl;
            return false;
        }
    }
    /// 3. Read climate site information from Climate database
    m_climStation = new InputStation(m_mongoClient, m_input->getDtHillslope(), m_input->getDtChannel());
    readClimateSiteList();

    /// 4. Read Mask raster data
    ostringstream oss;
    oss << m_subbasinID << "_" << Tag_Mask;
    string maskFileName = GetUpper(oss.str());
    m_maskRaster = FloatRaster::Init(m_spatialGridFS, maskFileName.c_str());
    assert(nullptr != m_maskRaster);
    m_rsMap.insert(make_pair(maskFileName, m_maskRaster));
    /// 5. Read Subbasin raster data
    oss.str("");
    oss << m_subbasinID << "_" << VAR_SUBBSN;
    string subbasinFileName = GetUpper(oss.str());
    FloatRaster *subbasinRaster = FloatRaster::Init(m_spatialGridFS,
                                                             subbasinFileName.c_str(),
                                                             true, m_maskRaster);
    assert(nullptr != subbasinRaster);
    m_rsMap.insert(make_pair(subbasinFileName, subbasinRaster));
    // Constructor Subbasin data
    m_subbasins = clsSubbasins::Init(m_spatialGridFS, m_rsMap, m_subbasinID);
    assert(nullptr != m_subbasins);
    /// 6. Read initial parameters
    if (!readParametersInDB()) {
        return false;
    }
    dumpCaliParametersInDB();
    /// 7. Read Reaches data
    m_reaches = new clsReaches(m_mongoClient, m_modelName, DB_TAB_REACH);
    m_reaches->Update(m_initParameters);
    /// 8. Check if Scenario will be applied, Get scenario database if necessary
    if (ValueInVector(string(DB_TAB_SCENARIO), existedMainDBTabs) && m_scenarioID >= 0) {
        bson_t *query;
        query = bson_new();
        m_scenDBName = QueryDatabaseName(query, DB_TAB_SCENARIO);
        if (!m_scenDBName.empty()) {
            m_useScenario = true;
            m_scenario = new Scenario(m_mongoClient, m_scenDBName, m_subbasinID, m_scenarioID);
            if (setRasterForScenario()) {
                m_scenario->setRasterForEachBMP();
            }
        }
    }
    return true;
}

string DataCenterMongoDB::QueryDatabaseName(bson_t *query, const char *tabname) {
    unique_ptr<MongoCollection> collection(new MongoCollection(m_mongoClient->getCollection(m_modelName, tabname)));
    mongoc_cursor_t *cursor = collection->ExecuteQuery(query);
    const bson_t *doc;
    string dbname = "";
    while (mongoc_cursor_next(cursor, &doc)) {
        bson_iter_t iter;
        if (bson_iter_init(&iter, doc) && bson_iter_find(&iter, "DB")) {
            dbname = GetStringFromBsonIterator(&iter);
            break;
        } else {
            cout << "ERROR: The DB field does not exist in " << string(tabname) << endl;
        }
    }
    bson_destroy(query);
    mongoc_cursor_destroy(cursor);
    return dbname;
}

bool DataCenterMongoDB::getFileInStringVector() {
    if (m_fileIn1DStrs.empty()) {
        bson_t *b = bson_new();
        unique_ptr<MongoCollection>
            collection(new MongoCollection(m_mongoClient->getCollection(m_modelName, DB_TAB_FILE_IN)));
        mongoc_cursor_t *cursor = collection->ExecuteQuery(b);
        bson_error_t *err = nullptr;
        if (mongoc_cursor_error(cursor, err)) {
            cout << "ERROR: Nothing found in the collection: " << DB_TAB_FILE_IN << "." << endl;
            return false;
        }
        bson_iter_t itertor;
        const bson_t *bsonTable;
        while (mongoc_cursor_more(cursor) && mongoc_cursor_next(cursor, &bsonTable)) {
            vector<string> tokens(2);
            if (bson_iter_init_find(&itertor, bsonTable, Tag_ConfTag)) {
                tokens[0] = GetStringFromBsonIterator(&itertor);
            }
            if (bson_iter_init_find(&itertor, bsonTable, Tag_ConfValue)) {
                tokens[1] = GetStringFromBsonIterator(&itertor);
            }
            if (StringMatch(tokens[0], Tag_Mode)) {
                m_modelMode = tokens[1];
            }
            size_t sz = m_fileIn1DStrs.size(); // get the current number of rows
            m_fileIn1DStrs.resize(sz + 1);  // resize with one more row
            m_fileIn1DStrs[sz] = tokens[0] + "|" + tokens[1]; // keep the interface consistent
        }
        bson_destroy(b);
        mongoc_cursor_destroy(cursor);
    }
    return true;
}

bool DataCenterMongoDB::getFileOutVector() {
    if (!m_OriginOutItems.empty()) {
        return true;
    }
    bson_t *b = bson_new();
    unique_ptr<MongoCollection>
        collection(new MongoCollection(m_mongoClient->getCollection(m_modelName, DB_TAB_FILE_OUT)));
    mongoc_cursor_t *cursor = collection->ExecuteQuery(b);
    bson_error_t *err = NULL;
    if (mongoc_cursor_error(cursor, err)) {
        cout << "ERROR: Nothing found in the collection: " << DB_TAB_FILE_OUT << "." << endl;
        /// destroy
        bson_destroy(b);
        mongoc_cursor_destroy(cursor);
        return false;
    }
    bson_iter_t itertor;
    const bson_t *bsonTable;
    while (mongoc_cursor_more(cursor) && mongoc_cursor_next(cursor, &bsonTable)) {
        OrgOutItem tmpOutputItem;
        if (bson_iter_init_find(&itertor, bsonTable, Tag_OutputUSE)) {
            GetNumericFromBsonIterator(&itertor, tmpOutputItem.use);
        }
        if (bson_iter_init_find(&itertor, bsonTable, Tag_MODCLS)) {
            tmpOutputItem.modCls = GetStringFromBsonIterator(&itertor);
        }
        if (bson_iter_init_find(&itertor, bsonTable, Tag_OutputID)) {
            tmpOutputItem.outputID = GetStringFromBsonIterator(&itertor);
        }
        if (bson_iter_init_find(&itertor, bsonTable, Tag_OutputDESC)) {
            tmpOutputItem.descprition = GetStringFromBsonIterator(&itertor);
        }
        if (bson_iter_init_find(&itertor, bsonTable, Tag_FileName)) {
            tmpOutputItem.outFileName = GetStringFromBsonIterator(&itertor);
        }
        if (bson_iter_init_find(&itertor, bsonTable, Tag_AggType)) {
            tmpOutputItem.aggType = GetStringFromBsonIterator(&itertor);
        }
        if (bson_iter_init_find(&itertor, bsonTable, Tag_OutputUNIT)) {
            tmpOutputItem.unit = GetStringFromBsonIterator(&itertor);
        }
        if (bson_iter_init_find(&itertor, bsonTable, Tag_OutputSubbsn)) {
            tmpOutputItem.subBsn = GetStringFromBsonIterator(&itertor);
        }
        if (bson_iter_init_find(&itertor, bsonTable, Tag_StartTime)) {
            tmpOutputItem.sTimeStr = GetStringFromBsonIterator(&itertor);
        }
        if (bson_iter_init_find(&itertor, bsonTable, Tag_EndTime)) {
            tmpOutputItem.eTimeStr = GetStringFromBsonIterator(&itertor);
        }
        if (bson_iter_init_find(&itertor, bsonTable, Tag_Interval)) {
            GetNumericFromBsonIterator(&itertor, tmpOutputItem.interval);
        }
        if (bson_iter_init_find(&itertor, bsonTable, Tag_IntervalUnit)) {
            tmpOutputItem.intervalUnit = GetStringFromBsonIterator(&itertor);
        }
        if (tmpOutputItem.use <= 0) {
            continue;
        } else {
            m_OriginOutItems.push_back(tmpOutputItem);
        }
    }
    vector<OrgOutItem>(m_OriginOutItems).swap(m_OriginOutItems);
    // m_OriginOutItems.shrink_to_fit();
    /// destroy
    bson_destroy(b);
    mongoc_cursor_destroy(cursor);
    if (!m_OriginOutItems.empty()) {
        return true;
    }
    return false;
}

bool DataCenterMongoDB::getSubbasinNumberAndOutletID() {
    bson_t *b = BCON_NEW("$query", "{", PARAM_FLD_NAME, "{", "$in", "[", BCON_UTF8(VAR_OUTLETID),
                         BCON_UTF8(VAR_SUBBSNID_NUM),
                         "]", "}", "}");
    // printf("%s\n",bson_as_json(b, NULL));

    unique_ptr<MongoCollection>
        collection(new MongoCollection(m_mongoClient->getCollection(m_modelName, DB_TAB_PARAMETERS)));
    mongoc_cursor_t *cursor = collection->ExecuteQuery(b);
    bson_error_t *err = NULL;
    if (mongoc_cursor_error(cursor, err)) {
        cout << "ERROR: Nothing found for subbasin number and outlet ID." << endl;
        /// destroy
        bson_destroy(b);
        mongoc_cursor_destroy(cursor);
        return false;
    }

    bson_iter_t iter;
    const bson_t *bsonTable;
    while (mongoc_cursor_more(cursor) && mongoc_cursor_next(cursor, &bsonTable)) {
        string nameTmp = "";
        int numTmp = -1;
        if (bson_iter_init_find(&iter, bsonTable, PARAM_FLD_NAME)) {
            nameTmp = GetStringFromBsonIterator(&iter);
        }
        if (bson_iter_init_find(&iter, bsonTable, PARAM_FLD_VALUE)) {
            GetNumericFromBsonIterator(&iter, numTmp);
        }
        if (!StringMatch(nameTmp, "") && numTmp != -1) {
            if (StringMatch(nameTmp, VAR_OUTLETID)) {
                GetNumericFromBsonIterator(&iter, m_outletID);
            } else if (StringMatch(nameTmp, VAR_SUBBSNID_NUM)) {
                GetNumericFromBsonIterator(&iter, m_nSubbasins);
            }
        } else {
            cout << "ERROR: Nothing found for subbasin number and outlet ID." << endl;
        }
    }
    bson_destroy(b);
    mongoc_cursor_destroy(cursor);
    if (m_outletID >= 0 && m_nSubbasins >= 0) {
        return true;
    }
    return false;
}

void DataCenterMongoDB::readClimateSiteList() {
    bson_t *query;
    query = bson_new();
    // subbasin id
    BSON_APPEND_INT32(query, Tag_SubbasinId, m_subbasinID);
    // mode
    //string modelMode = m_input->getModelMode();
    BSON_APPEND_UTF8(query, Tag_Mode, m_input->getModelMode().c_str());

    unique_ptr<MongoCollection>
        collection(new MongoCollection(m_mongoClient->getCollection(m_modelName, DB_TAB_SITELIST)));
    mongoc_cursor_t *cursor = collection->ExecuteQuery(query);

    const bson_t *doc;
    while (mongoc_cursor_next(cursor, &doc)) {
        bson_iter_t iter;
        if (bson_iter_init(&iter, doc) && bson_iter_find(&iter, MONG_SITELIST_DB)) {
            m_climDBName = GetStringFromBsonIterator(&iter);
        } else {
            throw ModelException("DataCenterMongoDB", "Constructor", "The DB field does not exist in SiteList table.");
        }
        string siteList = "";
        if (bson_iter_init(&iter, doc) && bson_iter_find(&iter, SITELIST_TABLE_M)) {
            siteList = GetStringFromBsonIterator(&iter);
            for (int i = 0; i < METEO_VARS_NUM; ++i) {
                m_climStation->ReadSitesData(m_climDBName, siteList, METEO_VARS[i],
                                             m_input->getStartTime(), m_input->getEndTime(), m_input->isStormMode());
            }
        }

        if (bson_iter_init(&iter, doc) && bson_iter_find(&iter, SITELIST_TABLE_P)) {
            siteList = GetStringFromBsonIterator(&iter);
            m_climStation->ReadSitesData(m_climDBName, siteList, DataType_Precipitation,
                                         m_input->getStartTime(), m_input->getEndTime(), m_input->isStormMode());
        }

        if (bson_iter_init(&iter, doc) && bson_iter_find(&iter, SITELIST_TABLE_PET)) {
            siteList = GetStringFromBsonIterator(&iter);
            m_climStation->ReadSitesData(m_climDBName, siteList, DataType_PotentialEvapotranspiration,
                                         m_input->getStartTime(), m_input->getEndTime(), m_input->isStormMode());
        }
    }
    bson_destroy(query);
    mongoc_cursor_destroy(cursor);
}

bool DataCenterMongoDB::readParametersInDB() {
    bson_t *filter = bson_new();
    unique_ptr<MongoCollection>
        collection(new MongoCollection(m_mongoClient->getCollection(m_modelName, DB_TAB_PARAMETERS)));
    mongoc_cursor_t *cursor = collection->ExecuteQuery(filter);

    bson_error_t *err = NULL;
    const bson_t *info;
    if (mongoc_cursor_error(cursor, err)) {
        cout << "ERROR: Nothing found in the collection: " << DB_TAB_PARAMETERS << "." << endl;
        /// destroy
        bson_destroy(filter);
        mongoc_cursor_destroy(cursor);
        return false;
    }
    while (mongoc_cursor_more(cursor) && mongoc_cursor_next(cursor, &info)) {
        ParamInfo *p = new ParamInfo();
        bson_iter_t iter;
        if (bson_iter_init_find(&iter, info, PARAM_FLD_NAME)) {
            p->Name = GetStringFromBsonIterator(&iter);
        }
        if (bson_iter_init_find(&iter, info, PARAM_FLD_UNIT)) {
            p->Units = GetStringFromBsonIterator(&iter);
        }
        if (bson_iter_init_find(&iter, info, PARAM_FLD_VALUE)) {
            GetNumericFromBsonIterator(&iter, p->Value);
        }
        if (bson_iter_init_find(&iter, info, PARAM_FLD_CHANGE)) {
            p->Change = GetStringFromBsonIterator(&iter);
        }
        if (bson_iter_init_find(&iter, info, PARAM_FLD_IMPACT)) {
            GetNumericFromBsonIterator(&iter, p->Impact);
        }
        if (bson_iter_init_find(&iter, info, PARAM_FLD_MAX)) {
            GetNumericFromBsonIterator(&iter, p->Maximum);
        }
        if (bson_iter_init_find(&iter, info, PARAM_FLD_MIN)) {
            GetNumericFromBsonIterator(&iter, p->Minimun);
        }
        if (bson_iter_init_find(&iter, info, PARAM_CALI_VALUES) && m_calibrationID >= 0) {
            // Overwrite p->Impact according to calibration ID
            string cali_values_str = GetStringFromBsonIterator(&iter);
            vector<float> cali_values = SplitStringForFloat(cali_values_str, ',');
            if (m_calibrationID < cali_values.size()) {
                p->Impact = cali_values[m_calibrationID];
            }
        }
        if (!m_initParameters.insert(make_pair(GetUpper(p->Name), p)).second) {
            cout << "ERROR: Load parameter: " << GetUpper(p->Name) << " failed!" << endl;
            return false;
        }
        /// Special handling code for soil water capcity parameters
        /// e.g., SOL_AWC, SOL_UL, WILTINGPOINT. By ljzhu, 2018-1-11
        if (StringMatch(p->Name, VAR_SW_CAP)) {
            for (int si = 0; si < SOILWATER_VARS_NUM; si++) {
                ParamInfo *tmpp = new ParamInfo(*p);
                tmpp->Name = SOILWATER_VARS[si];
                m_initParameters.insert(make_pair(GetUpper(tmpp->Name), tmpp));
            }
        }
    }
    bson_destroy(filter);
    mongoc_cursor_destroy(cursor);
    return true;
}

FloatRaster *DataCenterMongoDB::readRasterData(const string &remoteFilename) {
    FloatRaster *rasterData = FloatRaster::Init(m_spatialGridFS, remoteFilename.c_str(),
                                                         true, m_maskRaster, true);
    assert(nullptr != rasterData);
    /// using insert() to make sure the successful insertion.
    if (!m_rsMap.insert(make_pair(remoteFilename, rasterData)).second) {
        delete rasterData;
        return nullptr;
    }
    return rasterData;
}

void DataCenterMongoDB::readItpWeightData(string &remoteFilename, int &num, float *&data) {
    clsITPWeightData *weightData = new clsITPWeightData(m_spatialGridFS, remoteFilename.c_str());
    weightData->getWeightData(&num, &data);
    if (!m_weightDataMap.insert(make_pair(remoteFilename, weightData)).second) {
        /// if insert data failed, release clsITPWeightData in case of memory leak
        delete weightData;
    }
}

void DataCenterMongoDB::read1DArrayData(string &paramName, string &remoteFilename,
                                        int &num, float *&data) {
    char *databuf = nullptr;
    size_t datalength;
    m_spatialGridFS->getStreamData(remoteFilename, databuf, datalength);
    float *floatValues = (float *) databuf;
    num = datalength / 4;
    data = (float *) databuf;
    if (!StringMatch(paramName, Tag_Weight)) {
        m_1DArrayMap.insert(make_pair(remoteFilename, data));
        m_1DLenMap.insert(make_pair(remoteFilename, num));
    }
}

void DataCenterMongoDB::read2DArrayData(string &remoteFilename, int &rows, int &cols, float **&data) {
    char *databuf = nullptr;
    size_t datalength;
    m_spatialGridFS->getStreamData(remoteFilename, databuf, datalength);
    float *floatValues = (float *) databuf;

    int nRows = (int) floatValues[0];
    int nCols = -1;
    rows = nRows;
    data = new float *[rows];
    //cout<<n<<endl;
    int index = 1;
    for (int i = 0; i < rows; i++) {
        int col = int(floatValues[index]); // real column
        if (nCols < 0) {
            nCols = col;
        } else if (nCols != col) {
            nCols = 1;
        }
        int nSub = col + 1;
        data[i] = new float[nSub];
        data[i][0] = col;
        //cout<<"index: "<<index<<",";
        for (int j = 1; j < nSub; j++) {
            data[i][j] = floatValues[index + j];
            //cout<<data[i][j]<<",";
        }
        //cout<<endl;
        index += nSub;
    }
    cols = nCols;
    /// release memory
    Release1DArray(floatValues);

    if (nullptr != databuf) {
        databuf = nullptr;
    }
    /// insert to corresponding maps
    m_2DArrayMap.insert(make_pair(remoteFilename, data));
    m_2DRowsLenMap.insert(make_pair(remoteFilename, nRows));
    m_2DColsLenMap.insert(make_pair(remoteFilename, nCols));
}

void DataCenterMongoDB::readIUHData(string &remoteFilename, int &n, float **&data) {
    char *databuf = nullptr;
    size_t datalength;
    m_spatialGridFS->getStreamData(remoteFilename, databuf, datalength);
    float *floatValues = (float *) databuf;

    n = (int) floatValues[0];
    data = new float *[n];

    int index = 1;
    for (int i = 0; i < n; i++) {
        int nSub = (int) (floatValues[index + 1] - floatValues[index] + 3);
        data[i] = new float[nSub];

        data[i][0] = floatValues[index];
        data[i][1] = floatValues[index + 1];
        for (int j = 2; j < nSub; j++) {
            data[i][j] = floatValues[index + j];
        }
        index = index + nSub;
    }
    /// release memory
    Release1DArray(floatValues);

    if (nullptr != databuf) {
        databuf = nullptr;
    }
    /// insert to corresponding maps
    m_2DArrayMap.insert(make_pair(remoteFilename, data));
    m_2DRowsLenMap.insert(make_pair(remoteFilename, n));
    m_2DColsLenMap.insert(make_pair(remoteFilename, 1));
}

bool DataCenterMongoDB::setRasterForScenario() {
    if (!m_useScenario) return false;
    if (nullptr == m_scenario) return false;
    map<string, FloatRaster *> &sceneRsMap = m_scenario->getSceneRasterDataMap();
    if (sceneRsMap.empty()) return false;
    for (auto it = sceneRsMap.begin(); it != sceneRsMap.end(); it++) {
        if (m_rsMap.find(it->first) == m_rsMap.end()) {
            it->second = readRasterData(it->first);
        } else {
            it->second = m_rsMap.at(it->first);
        }
    }
    return true;
}