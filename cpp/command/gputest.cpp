#include "../core/global.h"
#include "../core/config_parser.h"
#include "../core/fileutils.h"
#include "../dataio/sgf.h"
#include "../search/asyncbot.h"
#include "../program/setup.h"
#include "../program/playutils.h"
#include "../tests/tests.h"
#include "../command/commandline.h"
#include "../main.h"

#include <chrono>
#include <map>
#include <sstream>
#include <fstream>

using namespace std;


int MainCmds::testgpuerror(const vector<string>& args) {
  Board::initHash();
  ScoreValue::initTables();
  Rand seedRand;

  ConfigParser cfg;
  string modelFile;
  string boardSizeDataset;
  int defaultNNXSize;
  int defaultNNYSize;
  bool quickTest;
  string referenceFileName;
  try {
    KataGoCommandLine cmd("Test GPU error between FP16 and FP32 with and without batching");
    cmd.addConfigFileArg(KataGoCommandLine::defaultGtpConfigFileName(),"gtp_example.cfg");
    cmd.addModelFileArg();
    TCLAP::ValueArg<string> boardSizeDatasetArg("","boardsize", "Dataset to benchmark on (9,13,19,10x14,rectangle), default 19", false, "19", "SIZE");
    TCLAP::SwitchArg quickArg("","quick","Faster shorter test");
    cmd.add(boardSizeDatasetArg);
    cmd.add(quickArg);
    TCLAP::ValueArg<string> referenceFileArg("", "reference-file", "Reference file to be generated by Eigen backend; loaded by other backends for cross-backend check, if not specified then will use the backend's own FP32 as reference", false, "", "FILE");
    cmd.add(referenceFileArg);

    cmd.setShortUsageArgLimit();
    cmd.addOverrideConfigArg();

    cmd.parseArgs(args);

    modelFile = cmd.getModelFile();
    boardSizeDataset = boardSizeDatasetArg.getValue();
    quickTest = quickArg.getValue();
    referenceFileName = referenceFileArg.getValue();
    cmd.getConfig(cfg);

    if(boardSizeDataset == "19") {
      defaultNNXSize = 19;
      defaultNNYSize = 19;
    }
    else if(boardSizeDataset == "13") {
      defaultNNXSize = 13;
      defaultNNYSize = 13;
    }
    else if(boardSizeDataset == "9") {
      defaultNNXSize = 9;
      defaultNNYSize = 9;
    }
    else if(boardSizeDataset == "10x14") {
      defaultNNXSize = 10;
      defaultNNYSize = 14;
    }
    else if(boardSizeDataset == "rectangle") {
      defaultNNXSize = 19;
      defaultNNYSize = 19;
    }
    else {
      throw StringError("Board size dataset to test: invalid value " + boardSizeDataset);
    }
  }
  catch (TCLAP::ArgException &e) {
    cerr << "Error: " << e.error() << " for argument " << e.argId() << endl;
    return 1;
  }

  const bool logToStdoutDefault = true;
  const bool logToStderrDefault = false;
  const bool logTimeDefault = false;
  Logger logger(NULL, logToStdoutDefault, logToStderrDefault, logTimeDefault);
  logger.write("Version " + Version::getGitRevisionWithBackend());
  logger.write("Testing " + modelFile);
  logger.write("Testing on " + boardSizeDataset);
  logger.write("Testing average errors between different GPU configurations...");

  const string expectedSha256 = "";
  int maxBatchSize;
  if(cfg.contains("nnMaxBatchSize")) {
    maxBatchSize = cfg.getInt("nnMaxBatchSize", 1, 65536);
    logger.write("For batch test, using batch size from nnMaxBatchSize in config: " + Global::intToString(maxBatchSize));
  }
  else if(cfg.contains("numSearchThreads")) {
    maxBatchSize = cfg.getInt("numSearchThreads", 1, 65536);
    logger.write("For batch test, using batch size from numSearchThreads in config: " + Global::intToString(maxBatchSize));
  }
  else {
    maxBatchSize = 16;
    logger.write("For batch test, using default batch size 16");
  }
  const int expectedConcurrentEvals = maxBatchSize;

  const bool defaultRequireExactNNLen = false;

  NNEvaluator* nnEval;
  NNEvaluator* nnEval32;
  {
    logger.write("Initializing nneval using current config...");
    const bool disableFP16 = false;
    nnEval = Setup::initializeNNEvaluator(
      modelFile,modelFile,expectedSha256,cfg,logger,seedRand,expectedConcurrentEvals,
      defaultNNXSize,defaultNNYSize,maxBatchSize,defaultRequireExactNNLen,disableFP16,
      Setup::SETUP_FOR_BENCHMARK
    );
  }
  {
    if(nnEval->isAnyThreadUsingFP16()) {
      logger.write("Initializing nneval in fp32...");
      const bool disableFP16 = true;
      nnEval32 = Setup::initializeNNEvaluator(
        modelFile,modelFile,expectedSha256,cfg,logger,seedRand,expectedConcurrentEvals,
        defaultNNXSize,defaultNNYSize,maxBatchSize,defaultRequireExactNNLen,disableFP16,
        Setup::SETUP_FOR_BENCHMARK
      );
    }
    else {
      nnEval32 = nnEval;
    }
  }

  const double policyOptimismForTest = cfg.contains("policyOptimism") ? cfg.getDouble("policyOptimism") : 0.0;
  const double pdaForTest = cfg.contains("playoutDoublingAdvantage") ? cfg.getDouble("playoutDoublingAdvantage") : 0.0;
  const double nnPolicyTemperatureForTest = cfg.contains("rootPolicyTemperature") ? cfg.getDouble("rootPolicyTemperature") : 1.0;
  if(policyOptimismForTest != 0.0 || pdaForTest != 0.0 || nnPolicyTemperatureForTest != 1.0) {
    logger.write("Using policyOptimismForTest " + Global::doubleToString(policyOptimismForTest));
    logger.write("Using pdaForTest " + Global::doubleToString(pdaForTest));
    logger.write("Using nnPolicyTemperatureForTest " + Global::doubleToString(nnPolicyTemperatureForTest));
  }

  const int maxBatchSizeCap = -1;
  const bool verbose = true;
  bool fp32BatchSuccessBuf = true;
  bool success = Tests::runBackendErrorTest(
    nnEval,nnEval32,logger,boardSizeDataset,maxBatchSizeCap,verbose,quickTest,
    policyOptimismForTest,pdaForTest,nnPolicyTemperatureForTest,
    fp32BatchSuccessBuf,referenceFileName
  );
  (void)success;
  // cout << success << endl;

  if(nnEval32 != nnEval)
    delete nnEval32;
  delete nnEval;
  NeuralNet::globalCleanup();
  ScoreValue::freeTables();

  return success ? 0 : 1;
}
