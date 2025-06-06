#ifdef USE_OPENCL_BACKEND

#include "../neuralnet/openclhelpers.h"
#include "../neuralnet/opencltuner.h"
#include "../neuralnet/openclkernels.h"
#include "../neuralnet/modelversion.h"
#include "../core/fileutils.h"
#include "../core/rand.h"
#include "../core/makedir.h"
#include "../core/threadsafecounter.h"
#include "../dataio/homedata.h"

#include <cstring>

using namespace std;
using namespace OpenCLHelpers;

using half_t = half_float::half;

static map<string,int> readDescKeyValues(const string& fileName, const string& desc) {
  istringstream kvIn(desc);
  string kvChunk;
  map<string,int> keyValues;
  while(getline(kvIn,kvChunk,' '))
  {
    if(kvChunk.length() <= 0) continue;
    size_t equalsPos = kvChunk.find_first_of('=');
    if(equalsPos == string::npos) continue;
    string leftChunk = Global::trim(kvChunk.substr(0,equalsPos));
    string rightChunk = Global::trim(kvChunk.substr(equalsPos+1));
    if(leftChunk.length() == 0)
      throw IOError("OpenCLTuner readDescKeyValues: key value pair without key in: " + desc + " in file " + fileName);
    if(rightChunk.length() == 0)
      throw IOError("OpenCLTuner readDescKeyValues: key value pair without value in: " + desc + " in file " + fileName);
    if(keyValues.find(leftChunk) != keyValues.end())
      throw IOError("OpenCLTuner readDescKeyValues: duplicate key: " + leftChunk);
    int value;
    bool suc = Global::tryStringToInt(rightChunk, value);
    if(!suc)
      throw IOError("OpenCLTuner readDescKeyValues: could not parse value for key " + leftChunk + " in file " + fileName);

    keyValues[leftChunk] = value;
  }
  return keyValues;
}

static bool isMultipleOf(int x, int y) {
  return x % y == 0;
}

static int getInt(const map<string,int> map, const string& key, int defaultValue) {
  if(!contains(map,key))
    return defaultValue;
  return map_get(map,key);
}

string OpenCLParams::XGemmDirectParams::desc() const {
  string s;
  s += "WGD=" + Global::intToString(WGD);
  s += " MDIMCD=" + Global::intToString(MDIMCD);
  s += " NDIMCD=" + Global::intToString(NDIMCD);
  s += " MDIMAD=" + Global::intToString(MDIMAD);
  s += " NDIMBD=" + Global::intToString(NDIMBD);
  s += " KWID=" + Global::intToString(KWID);
  s += " VWMD=" + Global::intToString(VWMD);
  s += " VWND=" + Global::intToString(VWND);
  s += " PADA=" + Global::intToString(PADA);
  s += " PADB=" + Global::intToString(PADB);
  return s;
}
string OpenCLParams::XGemmDirectParams::compileOptions() const {
  string s;
  s += "-DWGD=" + Global::intToString(WGD);
  s += " -DMDIMCD=" + Global::intToString(MDIMCD);
  s += " -DNDIMCD=" + Global::intToString(NDIMCD);
  s += " -DMDIMAD=" + Global::intToString(MDIMAD);
  s += " -DNDIMBD=" + Global::intToString(NDIMBD);
  s += " -DKWID=" + Global::intToString(KWID);
  s += " -DVWMD=" + Global::intToString(VWMD);
  s += " -DVWND=" + Global::intToString(VWND);
  s += " -DPADA=" + Global::intToString(PADA);
  s += " -DPADB=" + Global::intToString(PADB);
  return s;
}
void OpenCLParams::XGemmDirectParams::fillFromDesc(const string& fileName, const string& desc) {
  map<string,int> kvs = readDescKeyValues(fileName, desc);
  WGD = getInt(kvs,"WGD",WGD);
  MDIMCD = getInt(kvs,"MDIMCD",MDIMCD);
  NDIMCD = getInt(kvs,"NDIMCD",NDIMCD);
  MDIMAD = getInt(kvs,"MDIMAD",MDIMAD);
  NDIMBD = getInt(kvs,"NDIMBD",NDIMBD);
  KWID = getInt(kvs,"KWID",KWID);
  VWMD = getInt(kvs,"VWMD",VWMD);
  VWND = getInt(kvs,"VWND",VWND);
  PADA = getInt(kvs,"PADA",PADA);
  PADB = getInt(kvs,"PADB",PADB);
}
bool OpenCLParams::XGemmDirectParams::isValid() const {
  if(WGD <= 0) return false;
  if(MDIMCD <= 0) return false;
  if(NDIMCD <= 0) return false;
  if(MDIMAD <= 0) return false;
  if(NDIMBD <= 0) return false;
  if(KWID <= 0) return false;
  if(VWMD <= 0) return false;
  if(VWND <= 0) return false;
  if(PADA < 0) return false;
  if(PADB < 0) return false;
  if(!isMultipleOf(WGD,KWID)) return false;
  if(!isMultipleOf(WGD,MDIMCD*VWMD)) return false;
  if(!isMultipleOf(WGD,NDIMCD*VWND)) return false;
  if(!isMultipleOf(WGD,MDIMAD*VWMD)) return false;
  if(!isMultipleOf(WGD,NDIMBD*VWND)) return false;
  if(!isMultipleOf(WGD,MDIMCD*NDIMCD/MDIMAD)) return false;
  if(!isMultipleOf(WGD,MDIMCD*NDIMCD/NDIMBD)) return false;
  return true;
}

string OpenCLParams::XGemmParams::desc() const {
  string s;
  s += "MWG=" + Global::intToString(MWG);
  s += " NWG=" + Global::intToString(NWG);
  s += " KWG=" + Global::intToString(KWG);
  s += " MDIMC=" + Global::intToString(MDIMC);
  s += " NDIMC=" + Global::intToString(NDIMC);
  s += " MDIMA=" + Global::intToString(MDIMA);
  s += " NDIMB=" + Global::intToString(NDIMB);
  s += " KWI=" + Global::intToString(KWI);
  s += " VWM=" + Global::intToString(VWM);
  s += " VWN=" + Global::intToString(VWN);
  s += " STRM=" + Global::intToString(STRM);
  s += " STRN=" + Global::intToString(STRN);
  s += " SA=" + Global::intToString(SA);
  s += " SB=" + Global::intToString(SB);
  return s;
}
string OpenCLParams::XGemmParams::compileOptions() const {
  string s;
  s += "-DMWG=" + Global::intToString(MWG);
  s += " -DNWG=" + Global::intToString(NWG);
  s += " -DKWG=" + Global::intToString(KWG);
  s += " -DMDIMC=" + Global::intToString(MDIMC);
  s += " -DNDIMC=" + Global::intToString(NDIMC);
  s += " -DMDIMA=" + Global::intToString(MDIMA);
  s += " -DNDIMB=" + Global::intToString(NDIMB);
  s += " -DKWI=" + Global::intToString(KWI);
  s += " -DVWM=" + Global::intToString(VWM);
  s += " -DVWN=" + Global::intToString(VWN);
  s += " -DSTRM=" + Global::intToString(STRM);
  s += " -DSTRN=" + Global::intToString(STRN);
  s += " -DSA=" + Global::intToString(SA);
  s += " -DSB=" + Global::intToString(SB);
  return s;
}
void OpenCLParams::XGemmParams::fillFromDesc(const string& fileName, const string& desc) {
  map<string,int> kvs = readDescKeyValues(fileName, desc);
  MWG = getInt(kvs,"MWG",MWG);
  NWG = getInt(kvs,"NWG",NWG);
  KWG = getInt(kvs,"KWG",KWG);
  MDIMC = getInt(kvs,"MDIMC",MDIMC);
  NDIMC = getInt(kvs,"NDIMC",NDIMC);
  MDIMA = getInt(kvs,"MDIMA",MDIMA);
  NDIMB = getInt(kvs,"NDIMB",NDIMB);
  KWI = getInt(kvs,"KWI",KWI);
  VWM = getInt(kvs,"VWM",VWM);
  VWN = getInt(kvs,"VWN",VWN);
  STRM = getInt(kvs,"STRM",STRM);
  STRN = getInt(kvs,"STRN",STRN);
  SA = getInt(kvs,"SA",SA);
  SB = getInt(kvs,"SB",SB);
}
bool OpenCLParams::XGemmParams::isValid() const {
  if(MWG <= 0) return false;
  if(NWG <= 0) return false;
  if(KWG <= 0) return false;
  if(MDIMC <= 0) return false;
  if(NDIMC <= 0) return false;
  if(MDIMA <= 0) return false;
  if(NDIMB <= 0) return false;
  if(KWI <= 0) return false;
  if(VWM <= 0) return false;
  if(VWN <= 0) return false;
  if(STRM < 0 || STRM > 1) return false;
  if(STRN < 0 || STRN > 1) return false;
  if(SA < 0 || SA > 1) return false;
  if(SB < 0 || SB > 1) return false;
  if(!isMultipleOf(KWG,KWI)) return false;
  if(!isMultipleOf(MWG,MDIMC*VWM)) return false;
  if(!isMultipleOf(NWG,NDIMC*VWN)) return false;
  if(!isMultipleOf(MWG,MDIMA*VWM)) return false;
  if(!isMultipleOf(NWG,NDIMB*VWN)) return false;
  if(!isMultipleOf(KWG,VWM)) return false;
  if(!isMultipleOf(KWG,MDIMC*NDIMC/MDIMA)) return false;
  if(!isMultipleOf(KWG,MDIMC*NDIMC/NDIMB)) return false;
  return true;
}
bool OpenCLParams::XGemmParams::isSimple() const {
  if(MDIMC != MDIMA) return false;
  if(NDIMC != NDIMB) return false;
  if(SA != SB) return false;
  if(VWM != VWN) return false;
  if(MWG != NWG) return false;
  return true;
}

string OpenCLParams::HGemmWmmaParams::desc() const {
  string s;
  s += "MWG=" + Global::intToString(MWG);
  s += " NWG=" + Global::intToString(NWG);
  s += " KWG=" + Global::intToString(KWG);
  s += " MWAVE=" + Global::intToString(MWAVE);
  s += " NWAVE=" + Global::intToString(NWAVE);
  s += " MWARP=" + Global::intToString(MWARP);
  s += " NWARP=" + Global::intToString(NWARP);
  s += " VWM=" + Global::intToString(VWM);
  s += " VWN=" + Global::intToString(VWN);
  s += " SA=" + Global::intToString(SA);
  s += " SB=" + Global::intToString(SB);
  return s;
}
string OpenCLParams::HGemmWmmaParams::compileOptions() const {
  string s;
  s += "-DMWG=" + Global::intToString(MWG);
  s += " -DNWG=" + Global::intToString(NWG);
  s += " -DKWG=" + Global::intToString(KWG);
  s += " -DMWAVE=" + Global::intToString(MWAVE);
  s += " -DNWAVE=" + Global::intToString(NWAVE);
  s += " -DMWARP=" + Global::intToString(MWARP);
  s += " -DNWARP=" + Global::intToString(NWARP);
  s += " -DVWM=" + Global::intToString(VWM);
  s += " -DVWN=" + Global::intToString(VWN);
  s += " -DSA=" + Global::intToString(SA);
  s += " -DSB=" + Global::intToString(SB);
  return s;
}
void OpenCLParams::HGemmWmmaParams::fillFromDesc(const string& fileName, const string& desc) {
  map<string,int> kvs = readDescKeyValues(fileName, desc);
  MWG = getInt(kvs,"MWG",MWG);
  NWG = getInt(kvs,"NWG",NWG);
  KWG = getInt(kvs,"KWG",KWG);
  MWAVE = getInt(kvs,"MWAVE",MWAVE);
  NWAVE = getInt(kvs,"NWAVE",NWAVE);
  MWARP = getInt(kvs,"MWARP",MWARP);
  NWARP = getInt(kvs,"NWARP",NWARP);
  VWM = getInt(kvs,"VWM",VWM);
  VWN = getInt(kvs,"VWN",VWN);
  SA = getInt(kvs,"SA",SA);
  SB = getInt(kvs,"SB",SB);
}
bool OpenCLParams::HGemmWmmaParams::isValid() const {
  if(MWG <= 0) return false;
  if(NWG <= 0) return false;
  if(KWG <= 0) return false;
  if(MWAVE <= 0) return false;
  if(NWAVE <= 0) return false;
  if(MWARP <= 0) return false;
  if(NWARP <= 0) return false;
  if(VWM <= 0) return false;
  if(VWN <= 0) return false;
  if(SA < 0 || SA > 1) return false;
  if(SB < 0 || SB > 1) return false;
  if(SA == 0 && VWM != 2) return false;
  if(SB == 0 && VWN != 2) return false;

  if(!isMultipleOf(MWG,VWM)) return false;
  if(!isMultipleOf(NWG,VWN)) return false;
  if(!isMultipleOf(MWG,MWAVE)) return false;
  if(!isMultipleOf(NWG,NWAVE)) return false;
  if(!isMultipleOf(MWAVE,MWARP)) return false;
  if(!isMultipleOf(NWAVE,NWARP)) return false;
  if(!isMultipleOf(KWG,16)) return false;
  if(!((MWARP == 8 && NWARP == 32) || (MWARP == 16 && NWARP == 16) || (MWARP == 32 && NWARP == 8))) return false;

  const int WARP_SIZE = 32;
  if(MWAVE/MWARP * WARP_SIZE * NWAVE/NWARP > 1024) return false;
  return true;
}

bool OpenCLParams::HGemmWmmaParams::isSimple() const {
  if(MWAVE != MWARP && MWAVE == MWG) return false;
  if(NWAVE != NWARP && NWAVE == NWG) return false;
  if(SA != SB) return false;
  if(VWM != VWN) return false;
  if(MWG != NWG) return false;
  return true;
}

int OpenCLParams::HGemmWmmaNCHWParams::getRequiredCDivisor() const {
  // Optimized hgemmWmmaNCHW only supports channel counts that are multiples of 32.
  return 32;
}

string OpenCLParams::HGemmWmmaNCHWParams::desc() const {
  string s;
  s += "MWG=" + Global::intToString(MWG);
  s += " NWG=" + Global::intToString(NWG);
  s += " KWG=" + Global::intToString(KWG);
  s += " MWAVE=" + Global::intToString(MWAVE);
  s += " NWAVE=" + Global::intToString(NWAVE);
  s += " MWARP=" + Global::intToString(MWARP);
  s += " NWARP=" + Global::intToString(NWARP);
  s += " VWM=" + Global::intToString(VWM);
  s += " VWN=" + Global::intToString(VWN);
  s += " SB=" + Global::intToString(SB);
  return s;
}
string OpenCLParams::HGemmWmmaNCHWParams::compileOptions() const {
  string s;
  s += "-DMWG=" + Global::intToString(MWG);
  s += " -DNWG=" + Global::intToString(NWG);
  s += " -DKWG=" + Global::intToString(KWG);
  s += " -DMWAVE=" + Global::intToString(MWAVE);
  s += " -DNWAVE=" + Global::intToString(NWAVE);
  s += " -DMWARP=" + Global::intToString(MWARP);
  s += " -DNWARP=" + Global::intToString(NWARP);
  s += " -DVWM=" + Global::intToString(VWM);
  s += " -DVWN=" + Global::intToString(VWN);
  s += " -DSB=" + Global::intToString(SB);
  return s;
}
void OpenCLParams::HGemmWmmaNCHWParams::fillFromDesc(const string& fileName, const string& desc) {
  map<string,int> kvs = readDescKeyValues(fileName, desc);
  MWG = getInt(kvs,"MWG",MWG);
  NWG = getInt(kvs,"NWG",NWG);
  KWG = getInt(kvs,"KWG",KWG);
  MWAVE = getInt(kvs,"MWAVE",MWAVE);
  NWAVE = getInt(kvs,"NWAVE",NWAVE);
  MWARP = getInt(kvs,"MWARP",MWARP);
  NWARP = getInt(kvs,"NWARP",NWARP);
  VWM = getInt(kvs,"VWM",VWM);
  VWN = getInt(kvs,"VWN",VWN);
  SB = getInt(kvs,"SB",SB);
}
bool OpenCLParams::HGemmWmmaNCHWParams::isValid() const {
  if(MWG <= 0) return false;
  if(NWG <= 0) return false;
  if(KWG <= 0) return false;
  if(MWAVE <= 0) return false;
  if(NWAVE <= 0) return false;
  if(MWARP <= 0) return false;
  if(NWARP <= 0) return false;
  if(VWM <= 0) return false;
  if(VWN <= 0) return false;
  if(SB < 0 || SB > 1) return false;
  if(SB == 0 && VWN != 2) return false;

  if(!isMultipleOf(MWG,VWM)) return false;
  if(!isMultipleOf(NWG,VWN)) return false;
  if(!isMultipleOf(MWG,MWAVE)) return false;
  if(!isMultipleOf(NWG,NWAVE)) return false;
  if(!isMultipleOf(MWAVE,MWARP)) return false;
  if(!isMultipleOf(NWAVE,NWARP)) return false;
  if(!isMultipleOf(KWG,16)) return false;
  if(!isMultipleOf(getRequiredCDivisor(),NWG)) return false;
  if(!isMultipleOf(getRequiredCDivisor(),KWG)) return false;
  if(!((MWARP == 8 && NWARP == 32) || (MWARP == 16 && NWARP == 16) || (MWARP == 32 && NWARP == 8))) return false;

  const int WARP_SIZE = 32;
  if(MWAVE/MWARP * WARP_SIZE * NWAVE/NWARP > 1024) return false;
  return true;
}

bool OpenCLParams::HGemmWmmaNCHWParams::isSimple() const {
  if(MWAVE != MWARP && MWAVE == MWG) return false;
  if(NWAVE != NWARP && NWAVE == NWG) return false;
  if(MWG != NWG) return false;
  return true;
}

string OpenCLParams::Conv3x3Params::desc() const {
  string s;
  s += "INTILE_XSIZE=" + Global::intToString(INTILE_XSIZE);
  s += " INTILE_YSIZE=" + Global::intToString(INTILE_YSIZE);
  s += " OUTTILE_XSIZE=" + Global::intToString(OUTTILE_XSIZE);
  s += " OUTTILE_YSIZE=" + Global::intToString(OUTTILE_YSIZE);
  s += " transLocalSize0=" + Global::intToString(transLocalSize0);
  s += " transLocalSize1=" + Global::intToString(transLocalSize1);
  s += " untransLocalSize0=" + Global::intToString(untransLocalSize0);
  s += " untransLocalSize1=" + Global::intToString(untransLocalSize1);
  s += " untransLocalSize2=" + Global::intToString(untransLocalSize2);
  return s;
}
string OpenCLParams::Conv3x3Params::transDesc() const {
  string s;
  s += " transLocalSize0=" + Global::intToString(transLocalSize0);
  s += " transLocalSize1=" + Global::intToString(transLocalSize1);
  return s;
}
string OpenCLParams::Conv3x3Params::untransDesc() const {
  string s;
  s += " untransLocalSize0=" + Global::intToString(untransLocalSize0);
  s += " untransLocalSize1=" + Global::intToString(untransLocalSize1);
  s += " untransLocalSize2=" + Global::intToString(untransLocalSize2);
  return s;
}
string OpenCLParams::Conv3x3Params::compileOptions() const {
  string s;
  s += "-DINTILE_XSIZE=" + Global::intToString(INTILE_XSIZE);
  s += " -DINTILE_YSIZE=" + Global::intToString(INTILE_YSIZE);
  s += " -DOUTTILE_XSIZE=" + Global::intToString(OUTTILE_XSIZE);
  s += " -DOUTTILE_YSIZE=" + Global::intToString(OUTTILE_YSIZE);
  s += " -DCONV_XSIZE=3 -DCONV_YSIZE=3 -DINTILE_XOFFSET=(-1) -DINTILE_YOFFSET=(-1)";
  return s;
}
void OpenCLParams::Conv3x3Params::fillFromDesc(const string& fileName, const string& desc) {
  map<string,int> kvs = readDescKeyValues(fileName, desc);
  INTILE_XSIZE = getInt(kvs,"INTILE_XSIZE",INTILE_XSIZE);
  INTILE_YSIZE = getInt(kvs,"INTILE_YSIZE",INTILE_YSIZE);
  OUTTILE_XSIZE = getInt(kvs,"OUTTILE_XSIZE",OUTTILE_XSIZE);
  OUTTILE_YSIZE = getInt(kvs,"OUTTILE_YSIZE",OUTTILE_YSIZE);
  transLocalSize0 = getInt(kvs,"transLocalSize0",transLocalSize0);
  transLocalSize1 = getInt(kvs,"transLocalSize1",transLocalSize1);
  untransLocalSize0 = getInt(kvs,"untransLocalSize0",untransLocalSize0);
  untransLocalSize1 = getInt(kvs,"untransLocalSize1",untransLocalSize1);
  untransLocalSize2 = getInt(kvs,"untransLocalSize2",untransLocalSize2);
}
bool OpenCLParams::Conv3x3Params::isValid() const {
  if(transLocalSize0 <= 0) return false;
  if(transLocalSize1 <= 0) return false;
  if(untransLocalSize0 <= 0) return false;
  if(untransLocalSize1 <= 0) return false;
  if(untransLocalSize2 <= 0) return false;

  if(transLocalSize0 * transLocalSize1 > 1024) return false;
  if(untransLocalSize0 * untransLocalSize1 * untransLocalSize2 > 1024) return false;

  //Currently, the only supported winograd tile sizes
  if(INTILE_XSIZE == 4 && OUTTILE_XSIZE == 2 && INTILE_YSIZE == 4 && OUTTILE_YSIZE == 2)
    return true;
  if(INTILE_XSIZE == 6 && OUTTILE_XSIZE == 4 && INTILE_YSIZE == 6 && OUTTILE_YSIZE == 4)
    return true;
  return false;
}


string OpenCLParams::Conv5x5Params::desc() const {
  string s;
  s += "INTILE_XSIZE=" + Global::intToString(INTILE_XSIZE);
  s += " INTILE_YSIZE=" + Global::intToString(INTILE_YSIZE);
  s += " OUTTILE_XSIZE=" + Global::intToString(OUTTILE_XSIZE);
  s += " OUTTILE_YSIZE=" + Global::intToString(OUTTILE_YSIZE);
  s += " transLocalSize0=" + Global::intToString(transLocalSize0);
  s += " transLocalSize1=" + Global::intToString(transLocalSize1);
  s += " untransLocalSize0=" + Global::intToString(untransLocalSize0);
  s += " untransLocalSize1=" + Global::intToString(untransLocalSize1);
  s += " untransLocalSize2=" + Global::intToString(untransLocalSize2);
  return s;
}
string OpenCLParams::Conv5x5Params::transDesc() const {
  string s;
  s += " transLocalSize0=" + Global::intToString(transLocalSize0);
  s += " transLocalSize1=" + Global::intToString(transLocalSize1);
  return s;
}
string OpenCLParams::Conv5x5Params::untransDesc() const {
  string s;
  s += " untransLocalSize0=" + Global::intToString(untransLocalSize0);
  s += " untransLocalSize1=" + Global::intToString(untransLocalSize1);
  s += " untransLocalSize2=" + Global::intToString(untransLocalSize2);
  return s;
}
string OpenCLParams::Conv5x5Params::compileOptions() const {
  string s;
  s += "-DINTILE_XSIZE=" + Global::intToString(INTILE_XSIZE);
  s += " -DINTILE_YSIZE=" + Global::intToString(INTILE_YSIZE);
  s += " -DOUTTILE_XSIZE=" + Global::intToString(OUTTILE_XSIZE);
  s += " -DOUTTILE_YSIZE=" + Global::intToString(OUTTILE_YSIZE);
  s += " -DCONV_XSIZE=5 -DCONV_YSIZE=5 -DINTILE_XOFFSET=(-2) -DINTILE_YOFFSET=(-2)";
  return s;
}
void OpenCLParams::Conv5x5Params::fillFromDesc(const string& fileName, const string& desc) {
  map<string,int> kvs = readDescKeyValues(fileName, desc);
  INTILE_XSIZE = getInt(kvs,"INTILE_XSIZE",INTILE_XSIZE);
  INTILE_YSIZE = getInt(kvs,"INTILE_YSIZE",INTILE_YSIZE);
  OUTTILE_XSIZE = getInt(kvs,"OUTTILE_XSIZE",OUTTILE_XSIZE);
  OUTTILE_YSIZE = getInt(kvs,"OUTTILE_YSIZE",OUTTILE_YSIZE);
  transLocalSize0 = getInt(kvs,"transLocalSize0",transLocalSize0);
  transLocalSize1 = getInt(kvs,"transLocalSize1",transLocalSize1);
  untransLocalSize0 = getInt(kvs,"untransLocalSize0",untransLocalSize0);
  untransLocalSize1 = getInt(kvs,"untransLocalSize1",untransLocalSize1);
  untransLocalSize2 = getInt(kvs,"untransLocalSize2",untransLocalSize2);
}
bool OpenCLParams::Conv5x5Params::isValid() const {
  if(transLocalSize0 <= 0) return false;
  if(transLocalSize1 <= 0) return false;
  if(untransLocalSize0 <= 0) return false;
  if(untransLocalSize1 <= 0) return false;
  if(untransLocalSize2 <= 0) return false;

  if(transLocalSize0 * transLocalSize1 > 1024) return false;
  if(untransLocalSize0 * untransLocalSize1 * untransLocalSize2 > 1024) return false;

  //Currently, the only supported winograd tile sizes
  if(INTILE_XSIZE == 6 && OUTTILE_XSIZE == 2 && INTILE_YSIZE == 6 && OUTTILE_YSIZE == 2)
    return true;
  return false;
}


string OpenCLParams::GPoolParams::desc() const {
  string s;
  s += "XYSTRIDE=" + Global::intToString(XYSTRIDE);
  s += " CHANNELSTRIDE=" + Global::intToString(CHANNELSTRIDE);
  s += " BATCHSTRIDE=" + Global::intToString(BATCHSTRIDE);
  return s;
}
string OpenCLParams::GPoolParams::compileOptions() const {
  string s;
  s += "-DXYSTRIDE=" + Global::intToString(XYSTRIDE);
  s += " -DCHANNELSTRIDE=" + Global::intToString(CHANNELSTRIDE);
  s += " -DBATCHSTRIDE=" + Global::intToString(BATCHSTRIDE);
  s += " -DLOCALSIZE_TOTAL=" + Global::intToString(XYSTRIDE * CHANNELSTRIDE * BATCHSTRIDE);
  return s;
}
void OpenCLParams::GPoolParams::fillFromDesc(const string& fileName, const string& desc) {
  map<string,int> kvs = readDescKeyValues(fileName, desc);
  XYSTRIDE = getInt(kvs,"XYSTRIDE",XYSTRIDE);
  CHANNELSTRIDE = getInt(kvs,"CHANNELSTRIDE",CHANNELSTRIDE);
  BATCHSTRIDE = getInt(kvs,"BATCHSTRIDE",BATCHSTRIDE);
}
bool OpenCLParams::GPoolParams::isValid() const {
  if(XYSTRIDE <= 0) return false;
  if(CHANNELSTRIDE <= 0) return false;
  if(BATCHSTRIDE <= 0) return false;

  //Must be power of 2
  if((XYSTRIDE & (XYSTRIDE-1)) != 0) return false;

  if(XYSTRIDE * CHANNELSTRIDE * BATCHSTRIDE > 1024) return false;

  return true;
}

bool OpenCLTuneParams::isValid() const {
  return
    xGemmDirect.isValid() &&
    xGemm.isValid() &&
    xGemm16.isValid() &&
    hGemmWmma.isValid() &&
    hGemmWmmaNCHW.isValid() &&
    conv3x3.isValid() &&
    conv5x5.isValid() &&
    gPool.isValid();
}

bool OpenCLTuneParams::operator==(const OpenCLTuneParams& other) const {
  if(this == &other)
    return true;
  return std::memcmp(this,&other,sizeof(OpenCLTuneParams)) == 0;
}

int OpenCLTuneParams::getXGemmMPaddingMult(bool usingFP16Compute, bool usingFP16TensorCores) const {
  if(usingFP16TensorCores) {
    return hGemmWmma.MWG;
  }
  if(usingFP16Compute) {
    return xGemm16.MWG;
  }
  return xGemm.MWG;
}
int OpenCLTuneParams::getXGemmNPaddingMult(bool usingFP16Compute, bool usingFP16TensorCores) const {
  if(usingFP16TensorCores) {
    return hGemmWmma.NWG;
  }
  if(usingFP16Compute) {
    return xGemm16.NWG;
  }
  return xGemm.NWG;
}
int OpenCLTuneParams::getXGemmKPaddingMult(bool usingFP16Compute, bool usingFP16TensorCores) const {
  if(usingFP16TensorCores) {
    return hGemmWmma.KWG;
  }
  if(usingFP16Compute) {
    return xGemm16.KWG;
  }
  return xGemm.KWG;
}


static const int TUNER_VERSION = 11;
static const char* TUNEPARAMS_VERSION_LINE = "VERSION=11";
void OpenCLTuneParams::save(const string& filename, const OpenCLTuneParams& config) {
  ofstream out;
  FileUtils::open(out,filename);
  out << TUNEPARAMS_VERSION_LINE << "\n";
  out << "#canUseFP16Storage" << "\n";
  out << config.canUseFP16Storage << "\n";
  out << "#canUseFP16Compute" << "\n";
  out << config.canUseFP16Compute << "\n";
  out << "#canUseFP16TensorCores" << "\n";
  out << config.canUseFP16TensorCores << "\n";
  out << "#canUseFP16TensorCoresFor1x1" << "\n";
  out << config.canUseFP16TensorCoresFor1x1 << "\n";
  out << "#shouldUseFP16Storage" << "\n";
  out << config.shouldUseFP16Storage << "\n";
  out << "#shouldUseFP16Compute" << "\n";
  out << config.shouldUseFP16Compute << "\n";
  out << "#shouldUseFP16TensorCores" << "\n";
  out << config.shouldUseFP16TensorCores << "\n";
  out << "#shouldUseFP16TensorCoresFor1x1" << "\n";
  out << config.shouldUseFP16TensorCoresFor1x1 << "\n";
  out << "#xGemmDirect" << "\n";
  out << config.xGemmDirect.desc() << "\n";
  out << "#xGemm" << "\n";
  out << config.xGemm.desc() << "\n";
  out << "#xGemm16" << "\n";
  out << config.xGemm16.desc() << "\n";
  out << "#hGemmWmma" << "\n";
  out << config.hGemmWmma.desc() << "\n";
  out << "#hGemmWmmaNCHW" << "\n";
  out << config.hGemmWmmaNCHW.desc() << "\n";
  out << "#conv3x3" << "\n";
  out << config.conv3x3.desc() << "\n";
  out << "#conv5x5" << "\n";
  out << config.conv5x5.desc() << "\n";
  out << "#gPool" << "\n";
  out << config.gPool.desc() << "\n";
  out.flush();
  out.close();
}


OpenCLTuneParams OpenCLTuneParams::load(const string& filename) {
  vector<string> lines = FileUtils::readFileLines(filename, '\n');
  vector<string> filteredLines;
  for(size_t i = 0; i<lines.size(); i++) {
    string line = Global::stripComments(lines[i]);
    line = Global::trim(line);
    if(line.length() > 0)
      filteredLines.push_back(line);
  }
  if(filteredLines.size() <= 0)
    throw IOError("OpenCLTuneParams::load: no params in file " + filename);
  if(filteredLines[0] != TUNEPARAMS_VERSION_LINE)
    throw IOError("OpenCLTuneParams::load: expected first line to be " + string(TUNEPARAMS_VERSION_LINE) + " in " + filename);

  if(filteredLines.size() != 17)
    throw IOError("OpenCLTuneParams::load: unexpected number of parameter lines in file " + filename);

  OpenCLTuneParams config;
  config.canUseFP16Storage = (bool)Global::stringToInt(filteredLines[1]);
  config.canUseFP16Compute = (bool)Global::stringToInt(filteredLines[2]);
  config.canUseFP16TensorCores = (bool)Global::stringToInt(filteredLines[3]);
  config.canUseFP16TensorCoresFor1x1 = (bool)Global::stringToInt(filteredLines[4]);
  config.shouldUseFP16Storage = (bool)Global::stringToInt(filteredLines[5]);
  config.shouldUseFP16Compute = (bool)Global::stringToInt(filteredLines[6]);
  config.shouldUseFP16TensorCores = (bool)Global::stringToInt(filteredLines[7]);
  config.shouldUseFP16TensorCoresFor1x1 = (bool)Global::stringToInt(filteredLines[8]);
  config.xGemmDirect.fillFromDesc(filename,filteredLines[9]);
  config.xGemm.fillFromDesc(filename,filteredLines[10]);
  config.xGemm16.fillFromDesc(filename,filteredLines[11]);
  config.hGemmWmma.fillFromDesc(filename,filteredLines[12]);
  config.hGemmWmmaNCHW.fillFromDesc(filename,filteredLines[13]);
  config.conv3x3.fillFromDesc(filename,filteredLines[14]);
  config.conv5x5.fillFromDesc(filename,filteredLines[15]);
  config.gPool.fillFromDesc(filename,filteredLines[16]);
  return config;
}

static cl_mem constantReadOnlyBufferFloat(cl_context context, int numElts, float constant) {
  vector<float> buf(numElts);
  for(int i = 0; i<numElts; i++)
    buf[i] = constant;
  return createReadOnlyBuffer(context,buf);
}
static cl_mem randomReadOnlyBufferFloat(const char* seed, cl_context context, int numElts, double scale, vector<float>& ret) {
  vector<float> buf(numElts);
  Rand rand(seed);
  for(int i = 0; i<numElts; i++)
    buf[i] = (float)rand.nextDouble(scale);
  ret = buf;
  return createReadOnlyBuffer(context,buf);
}
static cl_mem randomReadOnlyBufferHalf(const char* seed, cl_context context, int numElts, double scale, vector<float>& ret) {
  vector<half_t> buf(numElts);
  ret.resize(numElts);
  Rand rand(seed);
  for(int i = 0; i<numElts; i++) {
    double d = rand.nextDouble(scale);
    ret[i] = (float)d;
    buf[i] = half_float::half_cast<half_t>(d);
  }
  return createReadOnlyBuffer(context,buf);
}
static cl_mem randomReadOnly3dPaddedBufferFloat(
  const char* seed, cl_context context,
  int batchSize, int ySize, int ySizePadded, int xSize, int xSizePadded,
  double scale, vector<float>& ret
) {
  vector<float> buf((size_t)batchSize*ySizePadded*xSizePadded);
  Rand rand(seed);
  size_t i = 0;
  for(int n = 0; n<batchSize; n++) {
    for(int y = 0; y<ySizePadded; y++) {
      for(int x = 0; x<xSizePadded; x++) {
        if(y < ySize && x < xSize)
          buf[i++] = (float)rand.nextDouble(scale);
        else
          buf[i++] = 0.0f;
      }
    }
  }
  ret = buf;
  return createReadOnlyBuffer(context,buf);
}
static cl_mem randomReadOnly3dPaddedBufferHalf(
  const char* seed, cl_context context,
  int batchSize, int ySize, int ySizePadded, int xSize, int xSizePadded,
  double scale, vector<float>& ret
) {
  vector<half_t> buf((size_t)batchSize*ySizePadded*xSizePadded);
  ret.resize((size_t)batchSize*ySizePadded*xSizePadded);
  Rand rand(seed);
  size_t i = 0;
  for(int n = 0; n<batchSize; n++) {
    for(int y = 0; y<ySizePadded; y++) {
      for(int x = 0; x<xSizePadded; x++) {
        double d;
        if(y < ySize && x < xSize)
          d = rand.nextDouble(scale);
        else
          d = 0.0;

        ret[i] = (float)d;
        buf[i] = half_float::half_cast<half_t>(d);
        i += 1;
      }
    }
  }
  return createReadOnlyBuffer(context,buf);
}



template<typename T>
static void addConfigs(
  vector<OpenCLTuneParams>& configs,
  std::function<void(OpenCLTuneParams&, T value)> apply,
  const vector<T>& values
) {
  vector<OpenCLTuneParams> newCfgs;
  for(int i = 0; i<values.size(); i++) {
    for(int j = 0; j<configs.size(); j++) {
      OpenCLTuneParams cfg = configs[j];
      apply(cfg,values[i]);
      newCfgs.push_back(cfg);
    }
  }
  configs = newCfgs;
}

static void filterConfigs(
  vector<OpenCLTuneParams>& configs,
  std::function<bool(const OpenCLTuneParams&)> isValid
) {
  vector<OpenCLTuneParams> newCfgs;
  for(int j = 0; j<configs.size(); j++) {
    if(isValid(configs[j]))
      newCfgs.push_back(configs[j]);
  }
  configs = newCfgs;
}

static void shuffleConfigs(
  vector<OpenCLTuneParams>& configs
) {
  Rand rand;
  if(configs.size() == 0)
    return;
  for(size_t i = configs.size()-1; i > 0; i--) {
    size_t j = (size_t)rand.nextUInt64(i+1);
    std::swap(configs[i],configs[j]);
  }
}

static void dedupConfigsStable(
  vector<OpenCLTuneParams>& configs
) {
  vector<OpenCLTuneParams> deduped;
  for(size_t i = 0; i < configs.size(); i++) {
    bool foundDup = false;
    for(size_t j = 0; j < deduped.size(); j++) {
      if(configs[i] == deduped[j]) {
        foundDup = true;
        break;
      }
    }
    if(!foundDup)
      deduped.push_back(configs[i]);
  }
  configs = deduped;
}

struct OpenCLTuneAccums {
  bool bad = false;
  cl_int badErr = 0;
  string detailedErrorMessage;
  double weightCounted = 0;
  double weightedTimeTaken = 0;

  void countResultAndFreeEvent(cl_int err, cl_event event, double weight) {
    if(err != 0) {
      if(!bad) {
        bad = true;
        badErr = err;
      }
      return;
    }

    err = clWaitForEvents(1, &event);
    //If the kernel does bad things the error might also pop up here
    if(err != 0) {
      if(!bad) {
        bad = true;
        badErr = err;
      }
      return;
    }

    cl_ulong time_start, time_end;
    err = clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, sizeof(time_start), &time_start, NULL); CHECK_ERR(err);
    err = clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, sizeof(time_end), &time_end, NULL); CHECK_ERR(err);

    weightedTimeTaken += (time_end - time_start) * 1e-9 * weight;
    weightCounted += weight;

    clReleaseEvent(event);
  }

};

static bool testAllConfigs(
  bool stopOnReferenceImplFail,
  const vector<OpenCLTuneParams>& configsToTest,
  OpenCLTuneParams& currentConfig,
  OpenCLTuneParams referenceConfig,
  ostream& out,
  bool verboseErrors,
  bool verboseTuner,
  double errorToleranceScale,
  std::function<string(const OpenCLTuneParams&)> getDesc,
  std::function<OpenCLTuneAccums(const OpenCLTuneParams& cfg, vector<float>& ret, bool)> testConfig,
  double& bestKernelsPerSecondBuf
) {
  vector<OpenCLTuneParams> configs = configsToTest;

  //Insert the reference configuration first
  configs.insert(configs.begin(),referenceConfig);
  dedupConfigsStable(configs);

  double bestScore = 0.0;
  double bestKernelsPerSecond = 0.0;
  int lastBestIdx = 0;
  bool anythingGoodYet = false;
  int numTested = 0;
  int numTestedRunnable = 0;

  bool referenceRetIsFilled = false;
  vector<float> referenceRet;
  vector<float> ret;

  // First get a result computed on CPU to compare to.
  {
    const bool computeOnCPU = true;
    OpenCLTuneAccums cpuAccums = testConfig(referenceConfig,referenceRet,computeOnCPU);
    if(!cpuAccums.bad)
      referenceRetIsFilled = true;
  }

  out << "Testing " << configs.size() << " different configs" << endl;
  for(int i = 0; i<configs.size(); i++) {
    OpenCLTuneAccums accums = testConfig(configs[i],ret,false);

    numTested++;
    if(accums.bad) {
      if(verboseErrors) {
        out << "Tuning " << i << "/" << configs.size() << " failed: " << getErrorMessage(accums.badErr) << endl;
        if(accums.detailedErrorMessage.size() > 0)
          out << accums.detailedErrorMessage << endl;
      }
      if(i == 0) {
        if(stopOnReferenceImplFail)
          return false;
        out << "WARNING: Reference implementation failed: " << getErrorMessage(accums.badErr) << endl;
      }
    }
    else {
      if(!referenceRetIsFilled) {
        // There was no CPU result, so just use the first GPU result to compare error against.
        //Unless something has gone really weird, this should be the reference GPU implementation
        referenceRet = ret;
        referenceRetIsFilled = true;
      }

      numTestedRunnable++;

      double squerr = 0.0;
      double sqmag = 0.0;
      if(referenceRet.size() != ret.size())
        squerr = std::numeric_limits<double>::infinity();
      else {
        for(int j = 0; j<referenceRet.size(); j++) {
          if(!isfinite(referenceRet[j]) || !isfinite(ret[j]))
            squerr = std::numeric_limits<double>::infinity();
          else {
            double diff = (double)referenceRet[j] - (double)ret[j];
            squerr += diff * diff;
            sqmag += (double)referenceRet[j] * (double)referenceRet[j];
          }
        }
      }

      double kernelsPerSecond = accums.weightCounted / accums.weightedTimeTaken;
      double errorProp = sqrt(squerr / (sqmag + 1e-30));
      if(!isfinite(errorProp) || errorProp > 0.5)
        errorProp = 1.0;
      double errorPenaltyFactor = 1.0 - sqrt(errorProp / (errorProp + errorToleranceScale));
      if(errorProp > 1e-5)
        errorPenaltyFactor *= 0.90;
      if(errorProp > 0.5)
        errorPenaltyFactor = 0.0;

      double score = kernelsPerSecond * errorPenaltyFactor;
      if(verboseTuner || score > bestScore || !anythingGoodYet) {
        out << "Tuning "
            << (!verboseTuner ? "" : score > bestScore ? "* " : "  ")
            << i << "/"  << configs.size()
            << (i == 0 ? " (reference)" : "")
            << " Calls/sec " << kernelsPerSecond
            << " ErrorProp " << errorProp
            << " " << getDesc(configs[i]) << endl;
      }
      if(score > bestScore) {
        anythingGoodYet = true;
        bestKernelsPerSecond = kernelsPerSecond;
        bestScore = score;
        currentConfig = configs[i];
        lastBestIdx = i;
      }
    }
    if(i % 20 == 0 && i >= lastBestIdx+10)
      out << "Tuning " << i << "/" << configs.size() << " ..." << endl;
  }
  if(!anythingGoodYet) {
    out << "ERROR: Could not find any configuration that worked" << endl;
    return false;
  }

  bestKernelsPerSecondBuf = bestKernelsPerSecond;
  return true;
}

#define SETTER(field) std::function<void(OpenCLTuneParams&, int value)>([](OpenCLTuneParams& p, int value) noexcept { p.field = value; })
#define ISVALID(field) std::function<bool(const OpenCLTuneParams&)>([](const OpenCLTuneParams& p) noexcept { return p.field.isValid(); })
#define ISSIMPLE(field) std::function<bool(const OpenCLTuneParams&)>([](const OpenCLTuneParams& p) noexcept { return p.field.isSimple(); })

OpenCLTuner::ModelInfoForTuning OpenCLTuner::ModelInfoForTuning::ofDesc(const ModelDesc* desc) {
  OpenCLTuner::ModelInfoForTuning modelInfo;
  modelInfo.maxConvChannels1x1 = desc->maxConvChannels(1,1);
  modelInfo.maxConvChannels3x3 = desc->maxConvChannels(3,3);
  modelInfo.trunkNumChannels = desc->trunk.trunkNumChannels;
  modelInfo.midNumChannels = desc->trunk.midNumChannels;
  modelInfo.regularNumChannels = desc->trunk.regularNumChannels;
  modelInfo.gpoolNumChannels = desc->trunk.gpoolNumChannels;
  modelInfo.modelVersion = desc->modelVersion;
  return modelInfo;
}

static void cpuBatchedMatMul(
  const std::vector<float>& inputVec,
  const std::vector<float>& filterVec,
  float* outBase,
  int batchSize,
  int mSize, int nSize, int kSize,
  int inputStride, int filterStride, int outputStride
) {
  for(int b = 0; b<batchSize; b++) {
    for(int m2 = 0; m2<mSize; m2 += 16) {
      for(int n2 = 0; n2<nSize; n2 += 16) {
        //Zero out target
        for(int m = m2; m<m2+16 && m<mSize; m++) {
          for(int n = n2; n<n2+16 && n<nSize; n++) {
            outBase[b * outputStride + (m + n * mSize)] = 0.0f;
          }
        }
        for(int k = 0; k<kSize; k++) {
          for(int m = m2; m<m2+16 && m<mSize; m++) {
            for(int n = n2; n<n2+16 && n<nSize; n++) {
              outBase[b * outputStride + (m + n * mSize)] += inputVec[b * inputStride + (m + k * mSize)] * filterVec[b * filterStride + (n + k * nSize)];
            }
          }
        }
      }
    }
  }
}


static void tuneXGemmDirect(
  OpenCLTuneParams currentConfig,
  const OpenCLTuneParams& untunedConfig,
  const cl_context& context,
  cl_command_queue& commandQueue,
  const vector<cl_device_id>& deviceIdsToUse,
  int batchSize,
  int nnXLen,
  int nnYLen,
  OpenCLTuner::ModelInfoForTuning modelInfo,
  bool full,
  ostream& out,
  bool verboseErrors,
  bool verboseTuner,
  OpenCLTuneParams& tunedConfig,
  double& bestKernelsPerSecond
) {
  out << "------------------------------------------------------" << endl;
  out << "Tuning xGemmDirect for 1x1 convolutions and matrix mult" << endl;

  vector<OpenCLTuneParams> configs;
  configs.push_back(currentConfig);
  if(full) {
    addConfigs(configs,SETTER(xGemmDirect.WGD),{8,16,32,64});
    addConfigs(configs,SETTER(xGemmDirect.MDIMCD),{8,16,32});
    addConfigs(configs,SETTER(xGemmDirect.NDIMCD),{8,16,32});
    addConfigs(configs,SETTER(xGemmDirect.MDIMAD),{8,16,32});
    addConfigs(configs,SETTER(xGemmDirect.NDIMBD),{8,16,32});
    addConfigs(configs,SETTER(xGemmDirect.KWID),{2,8,16});
    addConfigs(configs,SETTER(xGemmDirect.VWMD),{1,2,4,8});
    addConfigs(configs,SETTER(xGemmDirect.VWND),{1,2,4,8});
    addConfigs(configs,SETTER(xGemmDirect.PADA),{1});
    addConfigs(configs,SETTER(xGemmDirect.PADB),{1});
  }
  else {
    addConfigs(configs,SETTER(xGemmDirect.WGD),{8,16,32});
    addConfigs(configs,SETTER(xGemmDirect.MDIMCD),{8,16,32});
    addConfigs(configs,SETTER(xGemmDirect.NDIMCD),{8,16,32});
    addConfigs(configs,SETTER(xGemmDirect.MDIMAD),{8,16,32});
    addConfigs(configs,SETTER(xGemmDirect.NDIMBD),{8,16,32});
    addConfigs(configs,SETTER(xGemmDirect.KWID),{2,8});
    addConfigs(configs,SETTER(xGemmDirect.VWMD),{2,4});
    addConfigs(configs,SETTER(xGemmDirect.VWND),{2,4});
    addConfigs(configs,SETTER(xGemmDirect.PADA),{1});
    addConfigs(configs,SETTER(xGemmDirect.PADB),{1});
  }

  filterConfigs(configs,ISVALID(xGemmDirect));
  shuffleConfigs(configs);

  OpenCLTuneParams referenceConfig = currentConfig;
  referenceConfig.xGemmDirect.WGD = untunedConfig.xGemmDirect.WGD;
  referenceConfig.xGemmDirect.MDIMCD = untunedConfig.xGemmDirect.MDIMCD;
  referenceConfig.xGemmDirect.NDIMCD = untunedConfig.xGemmDirect.NDIMCD;
  referenceConfig.xGemmDirect.MDIMAD = untunedConfig.xGemmDirect.MDIMAD;
  referenceConfig.xGemmDirect.NDIMBD = untunedConfig.xGemmDirect.NDIMBD;
  referenceConfig.xGemmDirect.KWID = untunedConfig.xGemmDirect.KWID;
  referenceConfig.xGemmDirect.VWMD = untunedConfig.xGemmDirect.VWMD;
  referenceConfig.xGemmDirect.VWND = untunedConfig.xGemmDirect.VWND;
  referenceConfig.xGemmDirect.PADA = untunedConfig.xGemmDirect.PADA;
  referenceConfig.xGemmDirect.PADB = untunedConfig.xGemmDirect.PADB;
  OpenCLTuneParams slightlyTunedConfig = referenceConfig;
  slightlyTunedConfig.xGemmDirect.MDIMCD = 8;
  slightlyTunedConfig.xGemmDirect.NDIMCD = 8;
  slightlyTunedConfig.xGemmDirect.MDIMAD = 8;
  slightlyTunedConfig.xGemmDirect.NDIMBD = 8;
  OpenCLTuneParams slightlyTunedConfig2 = slightlyTunedConfig;
  slightlyTunedConfig2.xGemmDirect.WGD = 16;

  configs.insert(configs.begin(),slightlyTunedConfig2);
  configs.insert(configs.begin(),slightlyTunedConfig);
  configs.insert(configs.begin(),currentConfig);

  auto getDesc = [](const OpenCLTuneParams& cfg) { return cfg.xGemmDirect.desc(); };

  auto test = [&](const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU) {
    OpenCLTuneAccums accums;

    cl_int err;
    cl_program program;
    string compileError;
    bool compileSuc = tryCompileProgram(
      "xgemmDirectProgram", context, deviceIdsToUse, OpenCLKernels::xgemmDirect,
      cfg.xGemmDirect.compileOptions() + " -DROUTINE_GEMMSTRIDEDBATCHED",
      program, compileError
    );
    if(!compileSuc) { accums.bad = true; accums.detailedErrorMessage = compileError; accums.badErr = CL_BUILD_PROGRAM_FAILURE; return accums; }
    cl_kernel kernel = clCreateKernel(program, "XgemmDirectStridedBatchedNN", &err);
    if(err != 0) { accums.bad = true; accums.badErr = err; return accums; }

    int maxChannels = modelInfo.maxConvChannels1x1;
    maxChannels = std::max(modelInfo.trunkNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.midNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.regularNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.gpoolNumChannels,maxChannels);

    int inputNumFloats = batchSize*nnXLen*nnYLen*maxChannels;
    int outputNumFloats = batchSize*nnXLen*nnYLen*maxChannels;
    int filterNumFloats = maxChannels * maxChannels;
    vector<float> inputVec;
    vector<float> filterVec;
    cl_mem input = randomReadOnlyBufferFloat("tuneXGemmDirectInput", context, inputNumFloats, 1.0, inputVec);
    cl_mem filter = randomReadOnlyBufferFloat("tuneXGemmDirectFilter", context, filterNumFloats, 1.0 / sqrt(maxChannels), filterVec);
    cl_mem output = createReadWriteBufferFloatZeros(context, outputNumFloats);

    const int reps = 18;
    const int numToRecord = 6;
    ret.resize(outputNumFloats*numToRecord, 0.0f);
    for(int i = 0; i<reps; i++) {
      int inChannels;
      int outChannels;
      double weight;
      switch(i % numToRecord) {
        //Weight 0 on first kernel call to warm up
      case 0: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.midNumChannels; weight = 0; break;
      case 1: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.midNumChannels; weight = 1; break;
      case 2: inChannels = modelInfo.midNumChannels; outChannels = modelInfo.trunkNumChannels; weight = 1; break;
      case 3: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.regularNumChannels; weight = 0.2; break;
      case 4: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.gpoolNumChannels; weight = 0.2; break;
      case 5: inChannels = maxChannels; outChannels = maxChannels; weight = 1; break;
      default: ASSERT_UNREACHABLE; break;
      }

      int filterStride = 0; //Reuse same filter for all matrices in batch
      int inputStride = nnXLen*nnYLen * inChannels;
      int outputStride = nnXLen*nnYLen * outChannels;

      if(computeOnCPU) {
        if(i >= numToRecord)
          continue;
        float* outBase = ret.data()+(outputNumFloats * i);
        //Replicating the way that parts outside the normal output are repeatedly written to the same buffer when outChannels is smaller.
        if(i > 0)
          std::copy(ret.begin()+(outputNumFloats*(i-1)), ret.begin()+(outputNumFloats*i), ret.begin()+(outputNumFloats*i));
        cpuBatchedMatMul(inputVec,filterVec,outBase,batchSize,nnXLen*nnYLen,outChannels,inChannels,inputStride,filterStride,outputStride);
        continue;
      }

      cl_event event;
      err = doStridedBatchedXGemmDirect_KM_KN_NM(
        kernel,
        commandQueue,
        cfg,
        nnXLen*nnYLen, outChannels, inChannels,
        inputStride, filterStride, outputStride,
        input, filter, output,
        batchSize,
        &event
      );


      accums.countResultAndFreeEvent(err,event,weight);
      if(accums.bad)
        break;
      if(i < numToRecord)
        blockingReadBuffer(commandQueue, output, outputNumFloats, ret.data()+(outputNumFloats * i));
    }

    if(accums.bad)
      ret.assign(outputNumFloats*numToRecord,0.0);

    clReleaseMemObject(input);
    clReleaseMemObject(filter);
    clReleaseMemObject(output);

    clReleaseKernel(kernel);
    clReleaseProgram(program);

    return accums;
  };

  bool stopOnReferenceImplFail = false;
  bestKernelsPerSecond = 0.0;
  double errorToleranceScale = 0.01;
  testAllConfigs(
    stopOnReferenceImplFail,
    configs,
    currentConfig,
    referenceConfig,
    out,
    verboseErrors,
    verboseTuner,
    errorToleranceScale,
    std::function<string(const OpenCLTuneParams& cfg)>(getDesc),
    std::function<OpenCLTuneAccums(const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU)>(test),
    bestKernelsPerSecond
  );
  tunedConfig = currentConfig;
}

static bool tuneXGemm(
  OpenCLTuneParams currentConfig,
  const OpenCLTuneParams& untunedConfig,
  const cl_context& context,
  cl_command_queue& commandQueue,
  const vector<cl_device_id>& deviceIdsToUse,
  int batchSize,
  int nnXLen,
  int nnYLen,
  OpenCLTuner::ModelInfoForTuning modelInfo,
  bool full,
  ostream& out,
  bool useFP16Storage,
  bool verboseErrors,
  bool verboseTuner,
  OpenCLTuneParams& tunedConfig,
  double& bestKernelsPerSecond
) {
  out << "------------------------------------------------------" << endl;
  if(useFP16Storage)
    out << "Tuning xGemm for convolutions - trying with FP16 storage" << endl;
  else
    out << "Tuning xGemm for convolutions" << endl;

  vector<OpenCLTuneParams> configs;
  configs.push_back(currentConfig);
  if(full) {
    addConfigs(configs,SETTER(xGemm.MWG),{8,16,32,64,128});
    addConfigs(configs,SETTER(xGemm.NWG),{8,16,32,64,128});
    addConfigs(configs,SETTER(xGemm.KWG),{8,16,32});
    addConfigs(configs,SETTER(xGemm.MDIMC),{8,16,32});
    addConfigs(configs,SETTER(xGemm.NDIMC),{8,16,32});
    addConfigs(configs,SETTER(xGemm.MDIMA),{8,16,32});
    addConfigs(configs,SETTER(xGemm.NDIMB),{8,16,32});
    addConfigs(configs,SETTER(xGemm.KWI),{2,8});
    addConfigs(configs,SETTER(xGemm.VWM),{1,2,4,8});
    addConfigs(configs,SETTER(xGemm.VWN),{1,2,4,8});
    addConfigs(configs,SETTER(xGemm.STRM),{0});
    addConfigs(configs,SETTER(xGemm.STRN),{0});
    addConfigs(configs,SETTER(xGemm.SA),{0,1});
    addConfigs(configs,SETTER(xGemm.SB),{0,1});
    filterConfigs(configs,ISVALID(xGemm));
  }
  else {
    addConfigs(configs,SETTER(xGemm.MWG),{16,32,64});
    addConfigs(configs,SETTER(xGemm.NWG),{16,32,64});
    addConfigs(configs,SETTER(xGemm.KWG),{16,32});
    addConfigs(configs,SETTER(xGemm.MDIMC),{8,16,32});
    addConfigs(configs,SETTER(xGemm.NDIMC),{8,16,32});
    addConfigs(configs,SETTER(xGemm.MDIMA),{8,16,32});
    addConfigs(configs,SETTER(xGemm.NDIMB),{8,16,32});
    addConfigs(configs,SETTER(xGemm.KWI),{2});
    addConfigs(configs,SETTER(xGemm.VWM),{2,4});
    addConfigs(configs,SETTER(xGemm.VWN),{2,4});
    addConfigs(configs,SETTER(xGemm.STRM),{0});
    addConfigs(configs,SETTER(xGemm.STRN),{0});
    addConfigs(configs,SETTER(xGemm.SA),{0,1});
    addConfigs(configs,SETTER(xGemm.SB),{0,1});
    filterConfigs(configs,ISVALID(xGemm));
    filterConfigs(configs,ISSIMPLE(xGemm));
  }

  shuffleConfigs(configs);

  OpenCLTuneParams referenceConfig = currentConfig;
  referenceConfig.xGemm.MWG = untunedConfig.xGemm.MWG;
  referenceConfig.xGemm.NWG = untunedConfig.xGemm.NWG;
  referenceConfig.xGemm.KWG = untunedConfig.xGemm.KWG;
  referenceConfig.xGemm.MDIMC = untunedConfig.xGemm.MDIMC;
  referenceConfig.xGemm.NDIMC = untunedConfig.xGemm.NDIMC;
  referenceConfig.xGemm.MDIMA = untunedConfig.xGemm.MDIMA;
  referenceConfig.xGemm.NDIMB = untunedConfig.xGemm.NDIMB;
  referenceConfig.xGemm.KWI = untunedConfig.xGemm.KWI;
  referenceConfig.xGemm.VWM = untunedConfig.xGemm.VWM;
  referenceConfig.xGemm.VWN = untunedConfig.xGemm.VWN;
  referenceConfig.xGemm.STRM = untunedConfig.xGemm.STRM;
  referenceConfig.xGemm.STRN = untunedConfig.xGemm.STRN;
  referenceConfig.xGemm.SA = untunedConfig.xGemm.SA;
  referenceConfig.xGemm.SB = untunedConfig.xGemm.SB;

  OpenCLTuneParams slightlyTunedConfig = referenceConfig;
  slightlyTunedConfig.xGemm.MDIMC = 8;
  slightlyTunedConfig.xGemm.NDIMC = 8;
  slightlyTunedConfig.xGemm.MDIMA = 8;
  slightlyTunedConfig.xGemm.NDIMB = 8;
  OpenCLTuneParams slightlyTunedConfig2 = slightlyTunedConfig;
  slightlyTunedConfig2.xGemm.MWG = 16;
  slightlyTunedConfig2.xGemm.NWG = 16;
  slightlyTunedConfig2.xGemm.KWG = 16;

  configs.insert(configs.begin(),slightlyTunedConfig2);
  configs.insert(configs.begin(),slightlyTunedConfig);
  configs.insert(configs.begin(),currentConfig);

  auto getDesc = [](const OpenCLTuneParams& cfg) { return cfg.xGemm.desc(); };

  auto test = [&](const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU) {
    OpenCLTuneAccums accums;

    cl_int err;
    cl_program program;
    string compileError;
    bool compileSuc = tryCompileProgram(
      "xgemmProgram", context, deviceIdsToUse, OpenCLKernels::xgemm,
      cfg.xGemm.compileOptions() + (useFP16Storage ? OpenCLKernels::fp16StorageDefine : ""),
      program, compileError
    );
    if(!compileSuc) { accums.bad = true; accums.detailedErrorMessage = compileError; accums.badErr = CL_BUILD_PROGRAM_FAILURE; return accums; }
    cl_kernel kernel = clCreateKernel(program, "XgemmBatched", &err);
    if(err != 0) { accums.bad = true; accums.badErr = err; return accums; }

    int numTilesX = (nnXLen + cfg.conv3x3.OUTTILE_XSIZE - 1) / cfg.conv3x3.OUTTILE_XSIZE;
    int numTilesY = (nnYLen + cfg.conv3x3.OUTTILE_YSIZE - 1) / cfg.conv3x3.OUTTILE_YSIZE;
    int numTilesTotal = batchSize * numTilesX * numTilesY;

    int inTileXSize = cfg.conv3x3.INTILE_XSIZE;
    int inTileYSize = cfg.conv3x3.INTILE_YSIZE;
    int inTileXYSize = inTileXSize * inTileYSize;

    int maxChannels = modelInfo.maxConvChannels3x3;
    maxChannels = std::max(modelInfo.trunkNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.midNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.regularNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.gpoolNumChannels,maxChannels);

    int numTilesTotalPadded = roundUpToMultipleInt(numTilesTotal,cfg.xGemm.MWG);
    int maxOutChannelsPadded = roundUpToMultipleInt(maxChannels,cfg.xGemm.NWG);
    int maxInChannelsPadded = roundUpToMultipleInt(maxChannels,cfg.xGemm.KWG);

    int outputNumFloats = numTilesTotalPadded * maxOutChannelsPadded * inTileXYSize;
    vector<float> inputVec;
    vector<float> filterVec;
    cl_mem input;
    cl_mem filter;
    cl_mem output;
    if(useFP16Storage) {
      input = randomReadOnly3dPaddedBufferHalf(
        "tuneXGemm3x3Input", context, inTileXYSize, maxChannels, maxInChannelsPadded, numTilesTotal, numTilesTotalPadded, 1.0, inputVec);
      filter = randomReadOnly3dPaddedBufferHalf(
        "tuneXGemm3x3Filter", context, inTileXYSize, maxChannels, maxInChannelsPadded, maxChannels, maxOutChannelsPadded, 1.0 / sqrt(maxChannels * 3 * 3), filterVec);
      output = createReadWriteBufferHalfZeros(context, outputNumFloats);
    }
    else {
      input = randomReadOnly3dPaddedBufferFloat(
        "tuneXGemm3x3Input", context, inTileXYSize, maxChannels, maxInChannelsPadded, numTilesTotal, numTilesTotalPadded, 1.0, inputVec);
      filter = randomReadOnly3dPaddedBufferFloat(
        "tuneXGemm3x3Filter", context, inTileXYSize, maxChannels, maxInChannelsPadded, maxChannels, maxOutChannelsPadded, 1.0 / sqrt(maxChannels * 3 * 3), filterVec);
      output = createReadWriteBufferFloatZeros(context, outputNumFloats);
    }

    const int reps = 18;
    const int numToRecord = 6;
    ret.resize(outputNumFloats*numToRecord, 0.0f);
    for(int i = 0; i<reps; i++) {
      int inChannels;
      int outChannels;
      double weight;
      switch(i % numToRecord) {
        //Weight 0 on first kernel call to warm up
      case 0: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.midNumChannels; weight = 0; break;
      case 1: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.midNumChannels; weight = 1; break;
      case 2: inChannels = modelInfo.midNumChannels; outChannels = modelInfo.trunkNumChannels; weight = 1; break;
      case 3: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.regularNumChannels; weight = 0.2; break;
      case 4: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.gpoolNumChannels; weight = 0.2; break;
      case 5: inChannels = maxChannels; outChannels = maxChannels; weight = 1; break;
      default: ASSERT_UNREACHABLE; break;
      }

      int outChannelsPadded = roundUpToMultipleInt(outChannels, cfg.xGemm.NWG);
      int inChannelsPadded = roundUpToMultipleInt(inChannels, cfg.xGemm.KWG);

      if(computeOnCPU) {
        if(i >= numToRecord)
          continue;
        float* outBase = ret.data()+(outputNumFloats * i);
        cpuBatchedMatMul(
          inputVec,filterVec,outBase,inTileXYSize,numTilesTotalPadded,outChannelsPadded,inChannelsPadded,
          numTilesTotalPadded*inChannelsPadded,outChannelsPadded*inChannelsPadded,numTilesTotalPadded*outChannelsPadded
        );
        continue;
      }

      cl_event event;
      err = doBatchedXGemm_KM_KN_NM(
        kernel,
        commandQueue,
        cfg.xGemm,
        numTilesTotalPadded, outChannelsPadded, inChannelsPadded,
        input, filter, output,
        inTileXYSize,
        &event
      );

      accums.countResultAndFreeEvent(err,event,weight);
      if(accums.bad)
        break;
      if(i < numToRecord)
        blockingReadBuffer(commandQueue, output, outputNumFloats, ret.data()+(outputNumFloats * i), useFP16Storage);
    }

    if(accums.bad)
      ret.assign(outputNumFloats*numToRecord,0.0);

    //Compact ret down to only what we were supposed to get, without padding
    {
      int i = 0;
      for(int n = 0; n<inTileXYSize; n++) {
        for(int y = 0; y<maxChannels; y++) {
          for(int x = 0; x<numTilesTotal; x++) {
            ret[i++] = ret[x + numTilesTotalPadded * (y + maxOutChannelsPadded * n)];
          }
        }
      }
      ret.resize(inTileXYSize * maxChannels * numTilesTotal);
    }

    clReleaseMemObject(input);
    clReleaseMemObject(filter);
    clReleaseMemObject(output);

    clReleaseKernel(kernel);
    clReleaseProgram(program);

    return accums;
  };

  bool stopOnReferenceImplFail = useFP16Storage;
  bestKernelsPerSecond = 0.0;
  double errorToleranceScale = 0.005;
  bool suc = testAllConfigs(
    stopOnReferenceImplFail,
    configs,
    currentConfig,
    referenceConfig,
    out,
    verboseErrors,
    verboseTuner,
    errorToleranceScale,
    std::function<string(const OpenCLTuneParams& cfg)>(getDesc),
    std::function<OpenCLTuneAccums(const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU)>(test),
    bestKernelsPerSecond
  );
  tunedConfig = currentConfig;
  return suc;
}

static bool tuneXGemm16(
  OpenCLTuneParams currentConfig,
  const OpenCLTuneParams& untunedConfig,
  const cl_context& context,
  cl_command_queue& commandQueue,
  const vector<cl_device_id>& deviceIdsToUse,
  int batchSize,
  int nnXLen,
  int nnYLen,
  OpenCLTuner::ModelInfoForTuning modelInfo,
  bool full,
  ostream& out,
  bool verboseErrors,
  bool verboseTuner,
  OpenCLTuneParams& tunedConfig,
  double& bestKernelsPerSecond
) {
  out << "------------------------------------------------------" << endl;
  out << "Tuning xGemm16 for convolutions" << endl;

  vector<OpenCLTuneParams> configs;
  configs.push_back(currentConfig);
  if(full) {
    addConfigs(configs,SETTER(xGemm16.MWG),{8,16,32,64,128});
    addConfigs(configs,SETTER(xGemm16.NWG),{8,16,32,64,128});
    addConfigs(configs,SETTER(xGemm16.KWG),{8,16,32});
    addConfigs(configs,SETTER(xGemm16.MDIMC),{8,16,32});
    addConfigs(configs,SETTER(xGemm16.NDIMC),{8,16,32});
    addConfigs(configs,SETTER(xGemm16.MDIMA),{8,16,32});
    addConfigs(configs,SETTER(xGemm16.NDIMB),{8,16,32});
    addConfigs(configs,SETTER(xGemm16.KWI),{2,8});
    addConfigs(configs,SETTER(xGemm16.VWM),{1,2,4,8});
    addConfigs(configs,SETTER(xGemm16.VWN),{1,2,4,8});
    addConfigs(configs,SETTER(xGemm16.STRM),{0});
    addConfigs(configs,SETTER(xGemm16.STRN),{0});
    addConfigs(configs,SETTER(xGemm16.SA),{0,1});
    addConfigs(configs,SETTER(xGemm16.SB),{0,1});
    filterConfigs(configs,ISVALID(xGemm16));
  }
  else {
    addConfigs(configs,SETTER(xGemm16.MWG),{16,32,64});
    addConfigs(configs,SETTER(xGemm16.NWG),{16,32,64});
    addConfigs(configs,SETTER(xGemm16.KWG),{16,32});
    addConfigs(configs,SETTER(xGemm16.MDIMC),{8,16,32});
    addConfigs(configs,SETTER(xGemm16.NDIMC),{8,16,32});
    addConfigs(configs,SETTER(xGemm16.MDIMA),{8,16,32});
    addConfigs(configs,SETTER(xGemm16.NDIMB),{8,16,32});
    addConfigs(configs,SETTER(xGemm16.KWI),{2});
    addConfigs(configs,SETTER(xGemm16.VWM),{2,4});
    addConfigs(configs,SETTER(xGemm16.VWN),{2,4});
    addConfigs(configs,SETTER(xGemm16.STRM),{0});
    addConfigs(configs,SETTER(xGemm16.STRN),{0});
    addConfigs(configs,SETTER(xGemm16.SA),{0,1});
    addConfigs(configs,SETTER(xGemm16.SB),{0,1});
    filterConfigs(configs,ISVALID(xGemm16));
    filterConfigs(configs,ISSIMPLE(xGemm16));
  }

  shuffleConfigs(configs);

  OpenCLTuneParams referenceConfig = currentConfig;
  referenceConfig.xGemm16.MWG = untunedConfig.xGemm16.MWG;
  referenceConfig.xGemm16.NWG = untunedConfig.xGemm16.NWG;
  referenceConfig.xGemm16.KWG = untunedConfig.xGemm16.KWG;
  referenceConfig.xGemm16.MDIMC = untunedConfig.xGemm16.MDIMC;
  referenceConfig.xGemm16.NDIMC = untunedConfig.xGemm16.NDIMC;
  referenceConfig.xGemm16.MDIMA = untunedConfig.xGemm16.MDIMA;
  referenceConfig.xGemm16.NDIMB = untunedConfig.xGemm16.NDIMB;
  referenceConfig.xGemm16.KWI = untunedConfig.xGemm16.KWI;
  referenceConfig.xGemm16.VWM = untunedConfig.xGemm16.VWM;
  referenceConfig.xGemm16.VWN = untunedConfig.xGemm16.VWN;
  referenceConfig.xGemm16.STRM = untunedConfig.xGemm16.STRM;
  referenceConfig.xGemm16.STRN = untunedConfig.xGemm16.STRN;
  referenceConfig.xGemm16.SA = untunedConfig.xGemm16.SA;
  referenceConfig.xGemm16.SB = untunedConfig.xGemm16.SB;

  OpenCLTuneParams slightlyTunedConfig = referenceConfig;
  slightlyTunedConfig.xGemm16.MDIMC = 8;
  slightlyTunedConfig.xGemm16.NDIMC = 8;
  slightlyTunedConfig.xGemm16.MDIMA = 8;
  slightlyTunedConfig.xGemm16.NDIMB = 8;
  OpenCLTuneParams slightlyTunedConfig2 = slightlyTunedConfig;
  slightlyTunedConfig2.xGemm16.MWG = 16;
  slightlyTunedConfig2.xGemm16.NWG = 16;
  slightlyTunedConfig2.xGemm16.KWG = 16;

  configs.insert(configs.begin(),slightlyTunedConfig2);
  configs.insert(configs.begin(),slightlyTunedConfig);
  configs.insert(configs.begin(),currentConfig);

  auto getDesc = [](const OpenCLTuneParams& cfg) { return cfg.xGemm16.desc(); };

  auto test = [&](const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU) {
    OpenCLTuneAccums accums;

    cl_int err;
    cl_program program;
    string compileError;
    bool compileSuc = tryCompileProgram(
      "xgemmProgram", context, deviceIdsToUse, OpenCLKernels::xgemm,
      cfg.xGemm16.compileOptions() + OpenCLKernels::fp16StorageDefine + OpenCLKernels::fp16ComputeDefine,
      program, compileError
    );
    if(!compileSuc) { accums.bad = true; accums.detailedErrorMessage = compileError; accums.badErr = CL_BUILD_PROGRAM_FAILURE; return accums; }
    cl_kernel kernel = clCreateKernel(program, "XgemmBatched", &err);
    if(err != 0) { accums.bad = true; accums.badErr = err; return accums; }

    int numTilesX = (nnXLen + cfg.conv3x3.OUTTILE_XSIZE - 1) / cfg.conv3x3.OUTTILE_XSIZE;
    int numTilesY = (nnYLen + cfg.conv3x3.OUTTILE_YSIZE - 1) / cfg.conv3x3.OUTTILE_YSIZE;
    int numTilesTotal = batchSize * numTilesX * numTilesY;

    int inTileXSize = cfg.conv3x3.INTILE_XSIZE;
    int inTileYSize = cfg.conv3x3.INTILE_YSIZE;
    int inTileXYSize = inTileXSize * inTileYSize;

    int maxChannels = modelInfo.maxConvChannels3x3;
    maxChannels = std::max(modelInfo.trunkNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.midNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.regularNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.gpoolNumChannels,maxChannels);

    int numTilesTotalPadded = roundUpToMultipleInt(numTilesTotal,cfg.xGemm16.MWG);
    int maxOutChannelsPadded = roundUpToMultipleInt(maxChannels,cfg.xGemm16.NWG);
    int maxInChannelsPadded = roundUpToMultipleInt(maxChannels,cfg.xGemm16.KWG);

    int outputNumFloats = numTilesTotalPadded * maxOutChannelsPadded * inTileXYSize;
    vector<float> inputVec;
    vector<float> filterVec;
    cl_mem input = randomReadOnly3dPaddedBufferHalf(
      "tuneXGemm3x3Input", context, inTileXYSize, maxChannels, maxInChannelsPadded, numTilesTotal, numTilesTotalPadded, 1.0, inputVec);
    cl_mem filter = randomReadOnly3dPaddedBufferHalf(
      "tuneXGemm3x3Filter", context, inTileXYSize, maxChannels, maxInChannelsPadded, maxChannels, maxOutChannelsPadded, 1.0 / sqrt(maxChannels * 3 * 3), filterVec);
    cl_mem output = createReadWriteBufferHalfZeros(context, outputNumFloats);

    const int reps = 18;
    const int numToRecord = 6;
    ret.resize(outputNumFloats*numToRecord, 0.0f);
    for(int i = 0; i<reps; i++) {
      int inChannels;
      int outChannels;
      double weight;
      switch(i % numToRecord) {
        //Weight 0 on first kernel call to warm up
      case 0: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.midNumChannels; weight = 0; break;
      case 1: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.midNumChannels; weight = 1; break;
      case 2: inChannels = modelInfo.midNumChannels; outChannels = modelInfo.trunkNumChannels; weight = 1; break;
      case 3: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.regularNumChannels; weight = 0.2; break;
      case 4: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.gpoolNumChannels; weight = 0.2; break;
      case 5: inChannels = maxChannels; outChannels = maxChannels; weight = 1; break;
      default: ASSERT_UNREACHABLE; break;
      }

      int outChannelsPadded = roundUpToMultipleInt(outChannels, cfg.xGemm16.NWG);
      int inChannelsPadded = roundUpToMultipleInt(inChannels, cfg.xGemm16.KWG);

      if(computeOnCPU) {
        if(i >= numToRecord)
          continue;
        float* outBase = ret.data()+(outputNumFloats * i);
        cpuBatchedMatMul(
          inputVec,filterVec,outBase,inTileXYSize,numTilesTotalPadded,outChannelsPadded,inChannelsPadded,
          numTilesTotalPadded*inChannelsPadded,outChannelsPadded*inChannelsPadded,numTilesTotalPadded*outChannelsPadded
        );
        continue;
      }

      cl_event event;
      err = doBatchedXGemm_KM_KN_NM(
        kernel,
        commandQueue,
        cfg.xGemm16,
        numTilesTotalPadded, outChannelsPadded, inChannelsPadded,
        input, filter, output,
        inTileXYSize,
        &event
      );

      accums.countResultAndFreeEvent(err,event,weight);
      if(accums.bad)
        break;
      if(i < numToRecord)
        blockingReadBufferHalfToFloat(commandQueue, output, outputNumFloats, ret.data()+(outputNumFloats * i));
    }

    if(accums.bad)
      ret.assign(outputNumFloats*numToRecord,0.0);

    //Compact ret down to only what we were supposed to get, without padding
    {
      int i = 0;
      for(int n = 0; n<inTileXYSize; n++) {
        for(int y = 0; y<maxChannels; y++) {
          for(int x = 0; x<numTilesTotal; x++) {
            ret[i++] = ret[x + numTilesTotalPadded * (y + maxOutChannelsPadded * n)];
          }
        }
      }
      ret.resize(inTileXYSize * maxChannels * numTilesTotal);
    }

    clReleaseMemObject(input);
    clReleaseMemObject(filter);
    clReleaseMemObject(output);

    clReleaseKernel(kernel);
    clReleaseProgram(program);

    return accums;
  };

  bool stopOnReferenceImplFail = true;
  bestKernelsPerSecond = 0.0;
  double errorToleranceScale = 0.005;
  bool suc = testAllConfigs(
    stopOnReferenceImplFail,
    configs,
    currentConfig,
    referenceConfig,
    out,
    verboseErrors,
    verboseTuner,
    errorToleranceScale,
    std::function<string(const OpenCLTuneParams& cfg)>(getDesc),
    std::function<OpenCLTuneAccums(const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU)>(test),
    bestKernelsPerSecond
  );
  if(suc) {
    tunedConfig = currentConfig;
  }
  return suc;
}


static bool tuneHGemmWmma(
  OpenCLTuneParams currentConfig,
  const OpenCLTuneParams& untunedConfig,
  const cl_context& context,
  cl_command_queue& commandQueue,
  const vector<cl_device_id>& deviceIdsToUse,
  int batchSize,
  int nnXLen,
  int nnYLen,
  OpenCLTuner::ModelInfoForTuning modelInfo,
  bool full,
  ostream& out,
  bool verboseErrors,
  bool verboseTuner,
  OpenCLTuneParams& tunedConfig,
  double& bestKernelsPerSecond
) {
  out << "------------------------------------------------------" << endl;
  out << "Tuning hGemmWmma for convolutions" << endl;

  vector<OpenCLTuneParams> configs;
  configs.push_back(currentConfig);
  if(full) {
    addConfigs(configs,SETTER(hGemmWmma.MWG),{16,32,64,128});
    addConfigs(configs,SETTER(hGemmWmma.NWG),{16,32,64,128});
    addConfigs(configs,SETTER(hGemmWmma.KWG),{16,32,64});
    addConfigs(configs,SETTER(hGemmWmma.MWAVE),{8,16,32,64});
    addConfigs(configs,SETTER(hGemmWmma.NWAVE),{8,16,32,64});
    addConfigs(configs,SETTER(hGemmWmma.MWARP),{8,16,32});
    addConfigs(configs,SETTER(hGemmWmma.NWARP),{8,16,32});
    addConfigs(configs,SETTER(hGemmWmma.VWM),{2,4,8});
    addConfigs(configs,SETTER(hGemmWmma.VWN),{2,4,8});
    addConfigs(configs,SETTER(hGemmWmma.SA),{0,1});
    addConfigs(configs,SETTER(hGemmWmma.SB),{0,1});
    filterConfigs(configs,ISVALID(hGemmWmma));
  }
  else {
    addConfigs(configs,SETTER(hGemmWmma.MWG),{16,32,64});
    addConfigs(configs,SETTER(hGemmWmma.NWG),{16,32,64});
    addConfigs(configs,SETTER(hGemmWmma.KWG),{16,32,64});
    addConfigs(configs,SETTER(hGemmWmma.MWAVE),{8,16,32,64});
    addConfigs(configs,SETTER(hGemmWmma.NWAVE),{8,16,32,64});
    addConfigs(configs,SETTER(hGemmWmma.MWARP),{8,16,32});
    addConfigs(configs,SETTER(hGemmWmma.NWARP),{8,16,32});
    addConfigs(configs,SETTER(hGemmWmma.VWM),{2,4});
    addConfigs(configs,SETTER(hGemmWmma.VWN),{2,4});
    addConfigs(configs,SETTER(hGemmWmma.SA),{0,1});
    addConfigs(configs,SETTER(hGemmWmma.SB),{0,1});
    filterConfigs(configs,ISVALID(hGemmWmma));
    filterConfigs(configs,ISSIMPLE(hGemmWmma));
  }

  shuffleConfigs(configs);

  OpenCLTuneParams referenceConfig = currentConfig;
  referenceConfig.hGemmWmma.MWG = untunedConfig.hGemmWmma.MWG;
  referenceConfig.hGemmWmma.NWG = untunedConfig.hGemmWmma.NWG;
  referenceConfig.hGemmWmma.KWG = untunedConfig.hGemmWmma.KWG;
  referenceConfig.hGemmWmma.MWAVE = untunedConfig.hGemmWmma.MWAVE;
  referenceConfig.hGemmWmma.NWAVE = untunedConfig.hGemmWmma.NWAVE;
  referenceConfig.hGemmWmma.MWARP = untunedConfig.hGemmWmma.MWARP;
  referenceConfig.hGemmWmma.NWARP = untunedConfig.hGemmWmma.NWARP;
  referenceConfig.hGemmWmma.VWM = untunedConfig.hGemmWmma.VWM;
  referenceConfig.hGemmWmma.VWN = untunedConfig.hGemmWmma.VWN;
  referenceConfig.hGemmWmma.SA = untunedConfig.hGemmWmma.SA;
  referenceConfig.hGemmWmma.SB = untunedConfig.hGemmWmma.SB;

  configs.insert(configs.begin(),currentConfig);

  auto getDesc = [](const OpenCLTuneParams& cfg) { return cfg.hGemmWmma.desc(); };

  auto test = [&](const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU) {
    OpenCLTuneAccums accums;

    cl_int err;
    cl_program program;
    string compileError;
    bool compileSuc = tryCompileProgram(
      "hgemmWmmaProgram", context, deviceIdsToUse, OpenCLKernels::hgemmWmma,
      cfg.hGemmWmma.compileOptions() + OpenCLKernels::fp16StorageDefine,
      program, compileError
    );
    if(!compileSuc) { accums.bad = true; accums.detailedErrorMessage = compileError; accums.badErr = CL_BUILD_PROGRAM_FAILURE; return accums; }
    cl_kernel kernel = clCreateKernel(program, "hgemmWmmaBatched", &err);
    if(err != 0) { accums.bad = true; accums.badErr = err; return accums; }

    int numTilesX = (nnXLen + cfg.conv3x3.OUTTILE_XSIZE - 1) / cfg.conv3x3.OUTTILE_XSIZE;
    int numTilesY = (nnYLen + cfg.conv3x3.OUTTILE_YSIZE - 1) / cfg.conv3x3.OUTTILE_YSIZE;
    int numTilesTotal = batchSize * numTilesX * numTilesY;

    int inTileXSize = cfg.conv3x3.INTILE_XSIZE;
    int inTileYSize = cfg.conv3x3.INTILE_YSIZE;
    int inTileXYSize = inTileXSize * inTileYSize;

    int maxChannels = modelInfo.maxConvChannels3x3;
    maxChannels = std::max(modelInfo.trunkNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.midNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.regularNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.gpoolNumChannels,maxChannels);

    int numTilesTotalPadded = roundUpToMultipleInt(numTilesTotal,cfg.hGemmWmma.MWG);
    int maxOutChannelsPadded = roundUpToMultipleInt(maxChannels,cfg.hGemmWmma.NWG);
    int maxInChannelsPadded = roundUpToMultipleInt(maxChannels,cfg.hGemmWmma.KWG);

    int outputNumFloats = numTilesTotalPadded * maxOutChannelsPadded * inTileXYSize;
    vector<float> inputVec;
    vector<float> filterVec;
    cl_mem input = randomReadOnly3dPaddedBufferHalf(
      "tuneHGemmWmma3x3Input", context, inTileXYSize, maxChannels, maxInChannelsPadded, numTilesTotal, numTilesTotalPadded, 1.0, inputVec);
    cl_mem filter = randomReadOnly3dPaddedBufferHalf(
      "tuneHGemmWmma3x3Filter", context, inTileXYSize, maxChannels, maxInChannelsPadded, maxChannels, maxOutChannelsPadded, 1.0 / sqrt(maxChannels * 3 * 3), filterVec);
    cl_mem output = createReadWriteBufferHalfZeros(context, outputNumFloats);

    const int reps = 18;
    const int numToRecord = 6;
    ret.resize(outputNumFloats*numToRecord, 0.0f);
    for(int i = 0; i<reps; i++) {
      int inChannels;
      int outChannels;
      double weight;
      switch(i % numToRecord) {
        //Weight 0 on first kernel call to warm up
      case 0: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.midNumChannels; weight = 0; break;
      case 1: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.midNumChannels; weight = 1; break;
      case 2: inChannels = modelInfo.midNumChannels; outChannels = modelInfo.trunkNumChannels; weight = 1; break;
      case 3: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.regularNumChannels; weight = 0.2; break;
      case 4: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.gpoolNumChannels; weight = 0.2; break;
      case 5: inChannels = maxChannels; outChannels = maxChannels; weight = 1; break;
      default: ASSERT_UNREACHABLE; break;
      }

      int outChannelsPadded = roundUpToMultipleInt(outChannels, cfg.hGemmWmma.NWG);
      int inChannelsPadded = roundUpToMultipleInt(inChannels, cfg.hGemmWmma.KWG);

      if(computeOnCPU) {
        if(i >= numToRecord)
          continue;
        float* outBase = ret.data()+(outputNumFloats * i);
        cpuBatchedMatMul(
          inputVec,filterVec,outBase,inTileXYSize,numTilesTotalPadded,outChannelsPadded,inChannelsPadded,
          numTilesTotalPadded*inChannelsPadded,outChannelsPadded*inChannelsPadded,numTilesTotalPadded*outChannelsPadded
        );
        continue;
      }

      cl_event event;
      err = doBatchedHGemmWmma_KM_KN_NM(
        kernel,
        commandQueue,
        cfg,
        numTilesTotalPadded, outChannelsPadded, inChannelsPadded,
        input, filter, output,
        inTileXYSize,
        &event
      );

      accums.countResultAndFreeEvent(err,event,weight);
      if(accums.bad)
        break;
      if(i < numToRecord)
        blockingReadBufferHalfToFloat(commandQueue, output, outputNumFloats, ret.data()+(outputNumFloats * i));
    }

    if(accums.bad)
      ret.assign(outputNumFloats*numToRecord,0.0);

    //Compact ret down to only what we were supposed to get, without padding
    {
      int i = 0;
      for(int n = 0; n<inTileXYSize; n++) {
        for(int y = 0; y<maxChannels; y++) {
          for(int x = 0; x<numTilesTotal; x++) {
            ret[i++] = ret[x + numTilesTotalPadded * (y + maxOutChannelsPadded * n)];
          }
        }
      }
      ret.resize(inTileXYSize * maxChannels * numTilesTotal);
    }

    clReleaseMemObject(input);
    clReleaseMemObject(filter);
    clReleaseMemObject(output);

    clReleaseKernel(kernel);
    clReleaseProgram(program);

    return accums;
  };

  bool stopOnReferenceImplFail = true;
  bestKernelsPerSecond = 0.0;
  double errorToleranceScale = 0.002;
  bool suc = testAllConfigs(
    stopOnReferenceImplFail,
    configs,
    currentConfig,
    referenceConfig,
    out,
    verboseErrors,
    verboseTuner,
    errorToleranceScale,
    std::function<string(const OpenCLTuneParams& cfg)>(getDesc),
    std::function<OpenCLTuneAccums(const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU)>(test),
    bestKernelsPerSecond
  );
  if(suc) {
    tunedConfig = currentConfig;
  }
  return suc;
}

static bool tuneHGemmWmmaNCHW(
  OpenCLTuneParams currentConfig,
  const OpenCLTuneParams& untunedConfig,
  const cl_context& context,
  cl_command_queue& commandQueue,
  const vector<cl_device_id>& deviceIdsToUse,
  int batchSize,
  int nnXLen,
  int nnYLen,
  OpenCLTuner::ModelInfoForTuning modelInfo,
  bool full,
  ostream& out,
  bool verboseErrors,
  bool verboseTuner,
  OpenCLTuneParams& tunedConfig,
  double& bestKernelsPerSecond
) {
  out << "------------------------------------------------------" << endl;
  out << "Tuning hGemmWmmaNCHW for 1x1 convolutions" << endl;

  vector<OpenCLTuneParams> configs;
  configs.push_back(currentConfig);
  if(full) {
    addConfigs(configs,SETTER(hGemmWmmaNCHW.MWG),{16,32,64,128});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.NWG),{16,32});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.KWG),{16,32,64});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.MWAVE),{8,16,32,64});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.NWAVE),{8,16,32});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.MWARP),{8,16,32});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.NWARP),{8,16,32});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.VWM),{1,2,4,8});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.VWN),{2,4,8});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.SB),{0,1});
    filterConfigs(configs,ISVALID(hGemmWmmaNCHW));
  }
  else {
    addConfigs(configs,SETTER(hGemmWmmaNCHW.MWG),{16,32,64});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.NWG),{16,32});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.KWG),{16,32,64});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.MWAVE),{8,16,32,64});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.NWAVE),{8,16,32});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.MWARP),{8,16,32});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.NWARP),{8,16,32});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.VWM),{1,2,4});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.VWN),{2,4});
    addConfigs(configs,SETTER(hGemmWmmaNCHW.SB),{0,1});
    filterConfigs(configs,ISVALID(hGemmWmmaNCHW));
    filterConfigs(configs,ISSIMPLE(hGemmWmmaNCHW));
  }

  shuffleConfigs(configs);

  OpenCLTuneParams referenceConfig = currentConfig;
  referenceConfig.hGemmWmmaNCHW.MWG = untunedConfig.hGemmWmmaNCHW.MWG;
  referenceConfig.hGemmWmmaNCHW.NWG = untunedConfig.hGemmWmmaNCHW.NWG;
  referenceConfig.hGemmWmmaNCHW.KWG = untunedConfig.hGemmWmmaNCHW.KWG;
  referenceConfig.hGemmWmmaNCHW.MWAVE = untunedConfig.hGemmWmmaNCHW.MWAVE;
  referenceConfig.hGemmWmmaNCHW.NWAVE = untunedConfig.hGemmWmmaNCHW.NWAVE;
  referenceConfig.hGemmWmmaNCHW.MWARP = untunedConfig.hGemmWmmaNCHW.MWARP;
  referenceConfig.hGemmWmmaNCHW.NWARP = untunedConfig.hGemmWmmaNCHW.NWARP;
  referenceConfig.hGemmWmmaNCHW.VWM = untunedConfig.hGemmWmmaNCHW.VWM;
  referenceConfig.hGemmWmmaNCHW.VWN = untunedConfig.hGemmWmmaNCHW.VWN;
  referenceConfig.hGemmWmmaNCHW.SB = untunedConfig.hGemmWmmaNCHW.SB;

  configs.insert(configs.begin(),currentConfig);

  auto getDesc = [](const OpenCLTuneParams& cfg) { return cfg.hGemmWmmaNCHW.desc(); };

  auto test = [&](const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU) {
    OpenCLTuneAccums accums;

    cl_int err;
    cl_program program;
    string compileError;
    bool compileSuc = tryCompileProgram(
      "hgemmWmmaNCHWProgram", context, deviceIdsToUse, OpenCLKernels::hgemmWmmaNCHW,
      cfg.hGemmWmmaNCHW.compileOptions() + OpenCLKernels::fp16StorageDefine,
      program, compileError
    );
    if(!compileSuc) { accums.bad = true; accums.detailedErrorMessage = compileError; accums.badErr = CL_BUILD_PROGRAM_FAILURE; return accums; }
    cl_kernel kernel = clCreateKernel(program, "hgemmWmmaNCHW", &err);
    if(err != 0) { accums.bad = true; accums.badErr = err; return accums; }

    int maxChannels = modelInfo.maxConvChannels1x1;
    maxChannels = std::max(modelInfo.trunkNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.midNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.regularNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.gpoolNumChannels,maxChannels);

    maxChannels = roundUpToMultipleInt(maxChannels,cfg.hGemmWmmaNCHW.getRequiredCDivisor());

    int outputNumFloats = batchSize * maxChannels * nnXLen * nnYLen;
    vector<float> inputVec;
    vector<float> filterVec;
    cl_mem input = randomReadOnly3dPaddedBufferHalf(
      "tuneHGemmWmma3x3Input", context, batchSize, maxChannels, maxChannels, nnXLen*nnYLen, nnXLen*nnYLen, 1.0, inputVec);
    cl_mem filter = randomReadOnly3dPaddedBufferHalf(
      "tuneHGemmWmma3x3Filter", context, batchSize, maxChannels, maxChannels, maxChannels, maxChannels, 1.0 / sqrt(maxChannels), filterVec);
    cl_mem output = createReadWriteBufferHalfZeros(context, outputNumFloats);

    const int reps = 18;
    const int numToRecord = 6;
    ret.resize(outputNumFloats*numToRecord, 0.0f);
    for(int i = 0; i<reps; i++) {
      int inChannels;
      int outChannels;
      double weight;
      switch(i % numToRecord) {
        //Weight 0 on first kernel call to warm up
      case 0: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.midNumChannels; weight = 0; break;
      case 1: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.midNumChannels; weight = 1; break;
      case 2: inChannels = modelInfo.midNumChannels; outChannels = modelInfo.trunkNumChannels; weight = 1; break;
      case 3: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.regularNumChannels; weight = 0.2; break;
      case 4: inChannels = modelInfo.trunkNumChannels; outChannels = modelInfo.gpoolNumChannels; weight = 0.2; break;
      case 5: inChannels = maxChannels; outChannels = maxChannels; weight = 1; break;
      default: ASSERT_UNREACHABLE; break;
      }

      int outChannelsPadded = roundUpToMultipleInt(outChannels, cfg.hGemmWmmaNCHW.getRequiredCDivisor());
      int inChannelsPadded = roundUpToMultipleInt(inChannels, cfg.hGemmWmmaNCHW.getRequiredCDivisor());

      if(computeOnCPU) {
        if(i >= numToRecord)
          continue;
        float* outBase = ret.data()+(outputNumFloats * i);
        cpuBatchedMatMul(
          inputVec,filterVec,outBase,batchSize,nnXLen*nnYLen,outChannelsPadded,inChannelsPadded,
          nnXLen*nnYLen*inChannelsPadded,0,nnXLen*nnYLen*outChannelsPadded
        );
        continue;
      }

      cl_event event;
      err = doHGemmWmma_NCHW_ICOC(
        kernel,
        commandQueue,
        cfg,
        batchSize, inChannelsPadded, nnXLen*nnYLen, outChannelsPadded,
        input, filter, output,
        &event
      );

      accums.countResultAndFreeEvent(err,event,weight);
      if(accums.bad)
        break;
      if(i < numToRecord)
        blockingReadBufferHalfToFloat(commandQueue, output, outputNumFloats, ret.data()+(outputNumFloats * i));
    }

    if(accums.bad)
      ret.assign(outputNumFloats*numToRecord,0.0);

    //Compact ret down to only what we were supposed to get, without padding
    {
      int i = 0;
      for(int n = 0; n<batchSize; n++) {
        for(int y = 0; y<maxChannels; y++) {
          for(int x = 0; x<nnXLen*nnYLen; x++) {
            ret[i++] = ret[x + nnXLen*nnYLen * (y + maxChannels * n)];
          }
        }
      }
      ret.resize(batchSize * maxChannels * nnXLen*nnYLen);
    }

    clReleaseMemObject(input);
    clReleaseMemObject(filter);
    clReleaseMemObject(output);

    clReleaseKernel(kernel);
    clReleaseProgram(program);

    return accums;
  };

  bool stopOnReferenceImplFail = true;
  bestKernelsPerSecond = 0.0;
  double errorToleranceScale = 0.002;
  bool suc = testAllConfigs(
    stopOnReferenceImplFail,
    configs,
    currentConfig,
    referenceConfig,
    out,
    verboseErrors,
    verboseTuner,
    errorToleranceScale,
    std::function<string(const OpenCLTuneParams& cfg)>(getDesc),
    std::function<OpenCLTuneAccums(const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU)>(test),
    bestKernelsPerSecond
  );
  if(suc) {
    tunedConfig = currentConfig;
  }
  return suc;
}


static void tuneTransform(
  OpenCLTuneParams currentConfig,
  const OpenCLTuneParams& untunedConfig,
  const cl_context& context,
  cl_command_queue& commandQueue,
  const vector<cl_device_id>& deviceIdsToUse,
  int batchSize,
  int nnXLen,
  int nnYLen,
  OpenCLTuner::ModelInfoForTuning modelInfo,
  bool full,
  ostream& out,
  const string& maybeFP16CompileOptions,
  bool verboseErrors,
  bool verboseTuner,
  OpenCLTuneParams& tunedConfig
) {
  out << "------------------------------------------------------" << endl;
  out << "Tuning winograd transform for convolutions" << endl;

  vector<OpenCLTuneParams> configs;
  configs.push_back(currentConfig);
  if(full) {
    addConfigs(configs,SETTER(conv3x3.transLocalSize0),{1,2,4,8,16,32,64,128});
    addConfigs(configs,SETTER(conv3x3.transLocalSize1),{1,2,4,8,16,32,64});
  }
  else {
    addConfigs(configs,SETTER(conv3x3.transLocalSize0),{1,2,4,8,16,32,64,128});
    addConfigs(configs,SETTER(conv3x3.transLocalSize1),{1,2,4,8,16,32});
  }

  filterConfigs(configs,ISVALID(conv3x3));
  shuffleConfigs(configs);
  configs.insert(configs.begin(),currentConfig);

  OpenCLTuneParams referenceConfig = currentConfig;
  referenceConfig.conv3x3.transLocalSize0 = untunedConfig.conv3x3.transLocalSize0;
  referenceConfig.conv3x3.transLocalSize1 = untunedConfig.conv3x3.transLocalSize1;

  auto getDesc = [](const OpenCLTuneParams& cfg) { return cfg.conv3x3.transDesc(); };

  auto test = [&](const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU) {
    OpenCLTuneAccums accums;
    // We just let the reference config GPU impl be values that all values are compared for error against rather than a CPU impl
    if(computeOnCPU) {
      accums.bad = true;
      return accums;
    }

    cl_int err;
    cl_program program;
    string compileError;
    bool compileSuc = tryCompileProgram(
      "winogradConv3x3NCHWTransformProgram", context, deviceIdsToUse, OpenCLKernels::winogradTransformNCHW,
      cfg.conv3x3.compileOptions() + maybeFP16CompileOptions,
      program, compileError
    );
    if(!compileSuc) { accums.bad = true; accums.detailedErrorMessage = compileError; accums.badErr = CL_BUILD_PROGRAM_FAILURE; return accums; }
    cl_kernel kernel = clCreateKernel(program, "transform", &err);
    if(err != 0) { accums.bad = true; accums.badErr = err; return accums; }

    int convSize = 3;
    int numTilesX = (nnXLen + cfg.conv3x3.OUTTILE_XSIZE - 1) / cfg.conv3x3.OUTTILE_XSIZE;
    int numTilesY = (nnYLen + cfg.conv3x3.OUTTILE_YSIZE - 1) / cfg.conv3x3.OUTTILE_YSIZE;
    int numTilesTotal = batchSize * numTilesX * numTilesY;

    int inTileXSize = cfg.conv3x3.INTILE_XSIZE;
    int inTileYSize = cfg.conv3x3.INTILE_YSIZE;

    int maxChannels = modelInfo.maxConvChannels3x3;
    maxChannels = std::max(modelInfo.trunkNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.midNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.regularNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.gpoolNumChannels,maxChannels);

    int mPaddingMult = cfg.getXGemmMPaddingMult(cfg.shouldUseFP16Compute, cfg.shouldUseFP16TensorCores);
    //int nPaddingMult = cfg.getXGemmNPaddingMult(cfg.shouldUseFP16Compute, cfg.shouldUseFP16TensorCores);
    int kPaddingMult = cfg.getXGemmKPaddingMult(cfg.shouldUseFP16Compute, cfg.shouldUseFP16TensorCores);

    int inputNumFloats = batchSize * nnXLen * nnYLen * maxChannels;
    int outputNumFloats = roundUpToMultipleInt(numTilesTotal,mPaddingMult) * roundUpToMultipleInt(maxChannels,kPaddingMult) * inTileXSize * inTileYSize;

    cl_mem input;
    cl_mem output;
    vector<float> inputVec;
    if(cfg.shouldUseFP16Storage) {
      input = randomReadOnlyBufferHalf("tune3x3TransInput", context, inputNumFloats, 1.0, inputVec);
      output = createReadWriteBufferHalfZeros(context, outputNumFloats);
    }
    else {
      input = randomReadOnlyBufferFloat("tune3x3TransInput", context, inputNumFloats, 1.0, inputVec);
      output = createReadWriteBufferFloatZeros(context, outputNumFloats);
    }

    const int reps = 20;
    const int numToRecord = 10;
    ret.resize(outputNumFloats*numToRecord, 0.0f);
    for(int i = 0; i<reps; i++) {
      int inChannels;
      double weight;
      switch(i % numToRecord) {
        //Weight 0 on first kernel call to warm up
      case 0: inChannels = modelInfo.trunkNumChannels; weight = 0; break;
      case 1: inChannels = modelInfo.trunkNumChannels; weight = 1; break;
      case 2: inChannels = modelInfo.midNumChannels; weight = 1; break;
      case 3: inChannels = maxChannels; weight = 1; break;
      case 4: inChannels = modelInfo.trunkNumChannels; weight = 1; break;
      case 5: inChannels = modelInfo.midNumChannels; weight = 1; break;
      case 6: inChannels = maxChannels; weight = 1; break;
      case 7: inChannels = modelInfo.trunkNumChannels; weight = 1; break;
      case 8: inChannels = modelInfo.midNumChannels; weight = 1; break;
      case 9: inChannels = maxChannels; weight = 1; break;
      default: ASSERT_UNREACHABLE; break;
      }

      cl_event event;
      err = doWinogradTransform(
        kernel,
        commandQueue,
        cfg,
        input,output,
        nnXLen,nnYLen,
        batchSize,numTilesX,numTilesY,mPaddingMult,
        inChannels,kPaddingMult,
        convSize,
        &event
      );

      accums.countResultAndFreeEvent(err,event,weight);
      if(accums.bad)
        break;
      if(i < numToRecord)
        blockingReadBuffer(commandQueue, output, outputNumFloats, ret.data()+(outputNumFloats * i), cfg.shouldUseFP16Storage);
    }

    if(accums.bad)
      ret.assign(outputNumFloats*numToRecord,0.0);

    clReleaseMemObject(input);
    clReleaseMemObject(output);

    clReleaseKernel(kernel);
    clReleaseProgram(program);

    return accums;
  };

  bool stopOnReferenceImplFail = false;
  double bestKernelsPerSecond = 0.0;
  double errorToleranceScale = 0.005;
  testAllConfigs(
    stopOnReferenceImplFail,
    configs,
    currentConfig,
    referenceConfig,
    out,
    verboseErrors,
    verboseTuner,
    errorToleranceScale,
    std::function<string(const OpenCLTuneParams& cfg)>(getDesc),
    std::function<OpenCLTuneAccums(const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU)>(test),
    bestKernelsPerSecond
  );

  tunedConfig = currentConfig;
}

static void tuneUntransform(
  OpenCLTuneParams currentConfig,
  const OpenCLTuneParams& untunedConfig,
  const cl_context& context,
  cl_command_queue& commandQueue,
  const vector<cl_device_id>& deviceIdsToUse,
  int batchSize,
  int nnXLen,
  int nnYLen,
  OpenCLTuner::ModelInfoForTuning modelInfo,
  bool full,
  ostream& out,
  const string& maybeFP16CompileOptions,
  bool verboseErrors,
  bool verboseTuner,
  OpenCLTuneParams& tunedConfig
) {
  out << "------------------------------------------------------" << endl;
  out << "Tuning winograd untransform for convolutions" << endl;

  vector<OpenCLTuneParams> configs;
  configs.push_back(currentConfig);
  if(full) {
    addConfigs(configs,SETTER(conv3x3.untransLocalSize0),{1,2,4,8,16,32,64});
    addConfigs(configs,SETTER(conv3x3.untransLocalSize1),{1,2,4,8,16,32,64});
    addConfigs(configs,SETTER(conv3x3.untransLocalSize2),{1,2,4,8,16,32});
  }
  else {
    addConfigs(configs,SETTER(conv3x3.untransLocalSize0),{1,2,8,16,32});
    addConfigs(configs,SETTER(conv3x3.untransLocalSize1),{1,2,4,16,32});
    addConfigs(configs,SETTER(conv3x3.untransLocalSize2),{1,2,4,8,16});
  }

  filterConfigs(configs,ISVALID(conv3x3));
  shuffleConfigs(configs);
  configs.insert(configs.begin(),currentConfig);

  OpenCLTuneParams referenceConfig = currentConfig;
  referenceConfig.conv3x3.untransLocalSize0 = untunedConfig.conv3x3.untransLocalSize0;
  referenceConfig.conv3x3.untransLocalSize1 = untunedConfig.conv3x3.untransLocalSize1;
  referenceConfig.conv3x3.untransLocalSize2 = untunedConfig.conv3x3.untransLocalSize2;

  auto getDesc = [](const OpenCLTuneParams& cfg) { return cfg.conv3x3.untransDesc(); };

  auto test = [&](const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU) {
    OpenCLTuneAccums accums;
    // We just let the reference config GPU impl be values that all values are compared for error against rather than a CPU impl
    if(computeOnCPU) {
      accums.bad = true;
      return accums;
    }

    cl_int err;
    cl_program program;
    string compileError;
    bool compileSuc = tryCompileProgram(
      "winogradConv3x3NCHWUntransformProgram", context, deviceIdsToUse, OpenCLKernels::winogradUntransformNCHW,
      cfg.conv3x3.compileOptions() + maybeFP16CompileOptions,
      program, compileError
    );
    if(!compileSuc) { accums.bad = true; accums.detailedErrorMessage = compileError; accums.badErr = CL_BUILD_PROGRAM_FAILURE; return accums; }
    cl_kernel kernel = clCreateKernel(program, "untransform", &err);
    if(err != 0) { accums.bad = true; accums.badErr = err; return accums; }

    int convSize = 3;
    int numTilesX = (nnXLen + cfg.conv3x3.OUTTILE_XSIZE - 1) / cfg.conv3x3.OUTTILE_XSIZE;
    int numTilesY = (nnYLen + cfg.conv3x3.OUTTILE_YSIZE - 1) / cfg.conv3x3.OUTTILE_YSIZE;
    int numTilesTotal = batchSize * numTilesX * numTilesY;

    int inTileXSize = cfg.conv3x3.INTILE_XSIZE;
    int inTileYSize = cfg.conv3x3.INTILE_YSIZE;

    int maxChannels = modelInfo.maxConvChannels3x3;
    maxChannels = std::max(modelInfo.trunkNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.midNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.regularNumChannels,maxChannels);
    maxChannels = std::max(modelInfo.gpoolNumChannels,maxChannels);

    int mPaddingMult = cfg.getXGemmMPaddingMult(cfg.shouldUseFP16Compute, cfg.shouldUseFP16TensorCores);
    int nPaddingMult = cfg.getXGemmNPaddingMult(cfg.shouldUseFP16Compute, cfg.shouldUseFP16TensorCores);
    //int kPaddingMult = cfg.getXGemmKPaddingMult(cfg.shouldUseFP16Compute, cfg.shouldUseFP16TensorCores);

    int inputNumFloats = roundUpToMultipleInt(numTilesTotal,mPaddingMult) * roundUpToMultipleInt(maxChannels,nPaddingMult) * inTileXSize * inTileYSize;
    int outputNumFloats = batchSize * nnXLen * nnYLen * maxChannels;

    cl_mem input;
    cl_mem output;
    vector<float> inputVec;
    if(cfg.shouldUseFP16Storage) {
      input = randomReadOnlyBufferHalf("tune3x3UntransInput", context, inputNumFloats, 1.0, inputVec);
      output = createReadWriteBufferHalfZeros(context, outputNumFloats);
    }
    else {
      input = randomReadOnlyBufferFloat("tune3x3UntransInput", context, inputNumFloats, 1.0, inputVec);
      output = createReadWriteBufferFloatZeros(context, outputNumFloats);
    }

    const int reps = 20;
    const int numToRecord = 10;
    ret.resize(outputNumFloats*numToRecord, 0.0f);
    for(int i = 0; i<reps; i++) {
      int outChannels;
      double weight;
      switch(i % numToRecord) {
        //Weight 0 on first kernel call to warm up
      case 0: outChannels = modelInfo.trunkNumChannels; weight = 0; break;
      case 1: outChannels = modelInfo.trunkNumChannels; weight = 1; break;
      case 2: outChannels = modelInfo.midNumChannels; weight = 1; break;
      case 3: outChannels = maxChannels; weight = 1; break;
      case 4: outChannels = modelInfo.trunkNumChannels; weight = 1; break;
      case 5: outChannels = modelInfo.midNumChannels; weight = 1; break;
      case 6: outChannels = maxChannels; weight = 1; break;
      case 7: outChannels = modelInfo.trunkNumChannels; weight = 1; break;
      case 8: outChannels = modelInfo.midNumChannels; weight = 1; break;
      case 9: outChannels = maxChannels; weight = 1; break;
      default: ASSERT_UNREACHABLE; break;
      }

      cl_event event;
      err = doWinogradUntransform(
        kernel,
        commandQueue,
        cfg,
        input,output,
        nnXLen,nnYLen,
        batchSize,numTilesX,numTilesY,mPaddingMult,
        outChannels,nPaddingMult,
        convSize,
        &event
      );

      accums.countResultAndFreeEvent(err,event,weight);
      if(accums.bad)
        break;
      if(i < numToRecord)
        blockingReadBuffer(commandQueue, output, outputNumFloats, ret.data()+(outputNumFloats * i), cfg.shouldUseFP16Storage);
    }

    if(accums.bad)
      ret.assign(outputNumFloats*numToRecord,0.0);

    clReleaseMemObject(input);
    clReleaseMemObject(output);

    clReleaseKernel(kernel);
    clReleaseProgram(program);

    return accums;
  };

  bool stopOnReferenceImplFail = false;
  double bestKernelsPerSecond = 0.0;
  double errorToleranceScale = 0.005;
  testAllConfigs(
    stopOnReferenceImplFail,
    configs,
    currentConfig,
    referenceConfig,
    out,
    verboseErrors,
    verboseTuner,
    errorToleranceScale,
    std::function<string(const OpenCLTuneParams& cfg)>(getDesc),
    std::function<OpenCLTuneAccums(const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU)>(test),
    bestKernelsPerSecond
  );

  tunedConfig = currentConfig;
}

static void tuneGPool(
  OpenCLTuneParams currentConfig,
  const OpenCLTuneParams& untunedConfig,
  const cl_context& context,
  cl_command_queue& commandQueue,
  const vector<cl_device_id>& deviceIdsToUse,
  int batchSize,
  int nnXLen,
  int nnYLen,
  OpenCLTuner::ModelInfoForTuning modelInfo,
  bool full,
  ostream& out,
  const string& maybeFP16CompileOptions,
  bool verboseErrors,
  bool verboseTuner,
  OpenCLTuneParams& tunedConfig
) {
  out << "------------------------------------------------------" << endl;
  out << "Tuning global pooling strides" << endl;

  vector<OpenCLTuneParams> configs;
  configs.push_back(currentConfig);

  auto powersOfTwoUpTo = [](int n) {
    vector<int> vec;
    for(int i = 1; i <= n; i *= 2)
      vec.push_back(i);
    return vec;
  };

  int numChannels = modelInfo.gpoolNumChannels;
  if(full) {
    addConfigs(configs,SETTER(gPool.XYSTRIDE),{1,2,4,8,16,32,64});
    addConfigs(configs,SETTER(gPool.CHANNELSTRIDE),powersOfTwoUpTo(std::min(64,numChannels)));
    addConfigs(configs,SETTER(gPool.BATCHSTRIDE),powersOfTwoUpTo(std::min(4,batchSize)));
  }
  else {
    addConfigs(configs,SETTER(gPool.XYSTRIDE),{1,2,4,8,16,32});
    addConfigs(configs,SETTER(gPool.CHANNELSTRIDE),powersOfTwoUpTo(std::min(32,numChannels)));
    addConfigs(configs,SETTER(gPool.BATCHSTRIDE),powersOfTwoUpTo(std::min(4,batchSize)));
  }

  filterConfigs(configs,ISVALID(gPool));
  shuffleConfigs(configs);
  configs.insert(configs.begin(),currentConfig);

  OpenCLTuneParams referenceConfig = currentConfig;
  referenceConfig.gPool.XYSTRIDE = untunedConfig.gPool.XYSTRIDE;
  referenceConfig.gPool.CHANNELSTRIDE = untunedConfig.gPool.CHANNELSTRIDE;
  referenceConfig.gPool.BATCHSTRIDE = untunedConfig.gPool.BATCHSTRIDE;

  auto getDesc = [](const OpenCLTuneParams& cfg) { return cfg.gPool.desc(); };

  auto test = [&](const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU) {
    OpenCLTuneAccums accums;
    // We just let the reference config GPU impl be values that all values are compared for error against rather than a CPU impl
    if(computeOnCPU) {
      accums.bad = true;
      return accums;
    }

    cl_int err;
    cl_program program;
    string compileError;
    bool compileSuc = tryCompileProgram(
      "gPoolChannelsNCHWMaskProgram", context, deviceIdsToUse, OpenCLKernels::gPoolChannelsNCHWMask,
      cfg.gPool.compileOptions() + maybeFP16CompileOptions,
      program, compileError
    );
    if(!compileSuc) { accums.bad = true; accums.detailedErrorMessage = compileError; accums.badErr = CL_BUILD_PROGRAM_FAILURE; return accums; }
    cl_kernel kernel = clCreateKernel(program, "gPoolChannelsNCHWMask", &err);
    if(err != 0) { accums.bad = true; accums.badErr = err; return accums; }

    int inputNumFloats = batchSize * nnXLen * nnYLen * numChannels;
    int outputNumFloats = batchSize * numChannels * 3;

    cl_mem input;
    vector<float> inputVec;
    if(cfg.shouldUseFP16Storage)
      input = randomReadOnlyBufferHalf("tuneGPoolInput", context, inputNumFloats, 1.0, inputVec);
    else
      input = randomReadOnlyBufferFloat("tuneGPoolInput", context, inputNumFloats, 1.0, inputVec);

    cl_mem mask = constantReadOnlyBufferFloat(context, batchSize*nnXLen*nnYLen, 1.0f);
    cl_mem maskSum = constantReadOnlyBufferFloat(context, batchSize, (float)(nnXLen*nnYLen));
    cl_mem output = createReadWriteBufferFloatZeros(context, outputNumFloats);

    const int reps = 20;
    const int numToRecord = 10;
    ret.resize(outputNumFloats*numToRecord, 0.0f);
    for(int i = 0; i<reps; i++) {
      double weight;
      switch(i % numToRecord) {
        //Weight 0 on first kernel call to warm up
      case 0: weight = 0; break;
      default: weight = 1; break;
      }

      cl_event event;
      err = performGPoolMask(
        kernel,
        commandQueue,
        cfg,
        batchSize, numChannels, nnXLen*nnYLen,
        input,output,mask,maskSum,
        &event
      );

      accums.countResultAndFreeEvent(err,event,weight);
      if(accums.bad)
        break;
      if(i < numToRecord)
        blockingReadBuffer(commandQueue, output, outputNumFloats, ret.data()+(outputNumFloats * i));
    }

    if(accums.bad)
      ret.assign(outputNumFloats*numToRecord,0.0);

    clReleaseMemObject(input);
    clReleaseMemObject(mask);
    clReleaseMemObject(maskSum);
    clReleaseMemObject(output);

    clReleaseKernel(kernel);
    clReleaseProgram(program);

    return accums;
  };

  bool stopOnReferenceImplFail = false;
  double bestKernelsPerSecond = 0.0;
  double errorToleranceScale = 0.005;
  testAllConfigs(
    stopOnReferenceImplFail,
    configs,
    currentConfig,
    referenceConfig,
    out,
    verboseErrors,
    verboseTuner,
    errorToleranceScale,
    std::function<string(const OpenCLTuneParams& cfg)>(getDesc),
    std::function<OpenCLTuneAccums(const OpenCLTuneParams& cfg, vector<float>& ret, bool computeOnCPU)>(test),
    bestKernelsPerSecond
  );

  tunedConfig = currentConfig;
}

static void dummyThreadLoop(
  const vector<DeviceInfo>& allDeviceInfos,
  Logger* logger,
  int gpuIdxForTuning,
  WaitableFlag& dummyInitializedOrDeadFlag,
  WaitableFlag& dummyShouldStopFlag
) {
  auto reportFailure = [&](const string& message) {
    // If we can't compile the kernel for the dummy thread, then just quit.
    if(logger) {
      logger->write("WARNING: Dummy thread to load the GPU while tuning failed");
      logger->write(message);
    }
    if(logger == NULL || (!logger->isLoggingToStdout() && !logger->isLoggingToStderr())) {
      cerr << "WARNING: Dummy thread to load the GPU while tuning failed" << endl;
      cerr << message << endl;
    }
  };
  if(logger) {
    logger->write("Dummy tuning thread starting");
  }

  const bool enableProfiling = false;
  DevicesContext devicesContext(allDeviceInfos, {gpuIdxForTuning}, logger, enableProfiling);

  const InitializedDevice* device = devicesContext.findGpuExn(gpuIdxForTuning);
  const cl_context& context = device->context;
  cl_command_queue commandQueue = device->commandQueue;
  const vector<cl_device_id>& deviceIdsToUse = { device->info.deviceId };

  OpenCLTuneParams cfg;
  cfg.xGemmDirect.MDIMCD = 8;
  cfg.xGemmDirect.NDIMCD = 8;
  cfg.xGemmDirect.MDIMAD = 8;
  cfg.xGemmDirect.NDIMBD = 8;

  cl_int err;
  string compileError;
  bool compileSuc;

  cl_program xGemmProgram;
  compileSuc = tryCompileProgram(
    "xgemmDirectProgram", context, deviceIdsToUse, OpenCLKernels::xgemmDirect,
    cfg.xGemmDirect.compileOptions() + " -DROUTINE_GEMMSTRIDEDBATCHED",
    xGemmProgram, compileError
  );
  if(!compileSuc) {
    reportFailure("Compile error: " + compileError);
    dummyInitializedOrDeadFlag.setPermanently(true);
    return;
  }

  cl_program addPointWiseProgram;
  compileSuc = tryCompileProgram(
    "addPointWiseProgram", context, deviceIdsToUse, OpenCLKernels::addPointWise,
    string(), addPointWiseProgram, compileError
  );
  if(!compileSuc) {
    reportFailure("Compile error: " + compileError);
    dummyInitializedOrDeadFlag.setPermanently(true);
    return;
  }

  cl_kernel xGemmKernel = clCreateKernel(xGemmProgram, "XgemmDirectStridedBatchedNN", &err);
  if(err != 0) {
    reportFailure("createKernel error code " + Global::intToString(err));
    dummyInitializedOrDeadFlag.setPermanently(true);
    return;
  }
  cl_kernel addPointWiseKernel = clCreateKernel(addPointWiseProgram, "addPointWise", &err);
  if(err != 0) {
    reportFailure("createKernel error code " + Global::intToString(err));
    dummyInitializedOrDeadFlag.setPermanently(true);
    return;
  }

  const int batchSize = 1;
  const int mSize = 97;
  const int kSize = 151;

  vector<float> matrixAVec;
  vector<float> matrixBVec;
  vector<float> matrixCVec;
  vector<float> matrixDVec;
  cl_mem matrixA = randomReadOnlyBufferFloat("dummyThreadA", context, kSize*kSize, 1.2 / kSize, matrixAVec);
  cl_mem matrixB = randomReadOnlyBufferFloat("dummyThreadB", context, kSize*kSize, 1.2 / kSize, matrixBVec);
  cl_mem matrixC = randomReadOnlyBufferFloat("dummyThreadC", context, mSize*kSize, 1.0, matrixCVec);
  cl_mem matrixD = randomReadOnlyBufferFloat("dummyThreadD", context, mSize*kSize, 1.0, matrixDVec);
  cl_mem buffer = createReadWriteBufferFloatZeros(context, mSize*kSize);
  cl_mem buffer2 = createReadWriteBufferFloatZeros(context, mSize*kSize);

  vector<float> output(mSize*kSize, 0.0f);

  // Batch size 1, so no strides
  int aStride = 0;
  int bStride = 0;
  int cStride = 0;

  Rand rand("dummyThreadLoop");
  dummyInitializedOrDeadFlag.setPermanently(true);

  double total = 0.0;
  bool first = true;
  while(!dummyShouldStopFlag.get()) {
    int which = rand.nextInt(0,6);
    if(first) {
      which = 4;
      first = false;
    }
    if(which == 0 || which == 1 || which == 2 || which == 3) {
      cl_event event;
      err = doStridedBatchedXGemmDirect_KM_KN_NM(
        xGemmKernel,
        commandQueue,
        cfg,
        mSize, kSize, kSize,
        aStride, bStride, cStride,
        buffer, ((which == 0 || which == 1) ? matrixA : matrixB), buffer2,
        batchSize,
        &event
      );

      if(err != 0) {
        reportFailure("doStridedBatchedXGemmDirect_KM_KN_NM error code " + Global::intToString(err));
        return;
      }
      err = clWaitForEvents(1, &event);
      //If the kernel does bad things the error might also pop up here
      if(err != 0) {
        reportFailure("doStridedBatchedXGemmDirect_KM_KN_NM error code " + Global::intToString(err));
        return;
      }

      clReleaseEvent(event);
      std::swap(buffer,buffer2);
    }
    else if(which == 4 || which == 5) {
      cl_event event;
      err = OpenCLHelpers::doAddPointWise(
        addPointWiseKernel, commandQueue, buffer, (which == 4 ? matrixC : matrixD), mSize*kSize, &event
      );

      if(err != 0) {
        reportFailure("doStridedBatchedXGemmDirect_KM_KN_NM error code " + Global::intToString(err));
        return;
      }
      err = clWaitForEvents(1, &event);
      //If the kernel does bad things the error might also pop up here
      if(err != 0) {
        reportFailure("doStridedBatchedXGemmDirect_KM_KN_NM error code " + Global::intToString(err));
        return;
      }
      clReleaseEvent(event);
    }
    else {
      blockingReadBuffer(commandQueue, buffer, mSize*kSize, output.data());
      float subTotal = 0.0f;
      for(int i = 0; i<mSize*kSize; i++)
        subTotal += output[i];
      total += (double)subTotal;
    }
  }
  (void)total;
  if(logger != NULL)
    logger->write("Tuning dummy thread numeric total: " + Global::doubleToString(total));


  clReleaseMemObject(matrixA);
  clReleaseMemObject(matrixB);
  clReleaseMemObject(matrixC);
  clReleaseMemObject(matrixD);
  clReleaseMemObject(buffer);
  clReleaseMemObject(buffer2);

  clReleaseKernel(addPointWiseKernel);
  clReleaseKernel(xGemmKernel);
  clReleaseProgram(addPointWiseProgram);
  clReleaseProgram(xGemmProgram);

  return;
}



void OpenCLTuner::tune(
  const OpenCLTuneParams& initialConfig,
  const vector<DeviceInfo>& allDeviceInfos,
  DevicesContext& devicesContext,
  int gpuIdx,
  int batchSize,
  int nnXLen,
  int nnYLen,
  enabled_t testFP16Mode,
  enabled_t testFP16StorageMode,
  enabled_t testFP16ComputeMode,
  enabled_t testFP16TensorCoresMode,
  OpenCLTuner::ModelInfoForTuning modelInfo,
  bool full,
  int winograd3x3TileSize,
  Logger* logger,
  ostream& out,
  bool verboseErrors,
  bool verboseTuner,
  OpenCLTuneParams& tunedConfig
) {
  const InitializedDevice* device = devicesContext.findGpuExn(gpuIdx);
  const cl_context& context = device->context;
  cl_command_queue commandQueue = device->commandQueue;
  const vector<cl_device_id>& deviceIdsToUse = { device->info.deviceId };

  out << "Beginning GPU tuning for " << device->info.name << " modelVersion " << modelInfo.modelVersion << " channels " << modelInfo.trunkNumChannels << endl;

  // Start a dummy thread to put a bunch of load on the GPU, so that we can encourage dynamic-clock-speed GPUs
  // to stay at a high setting during the tuning.
  WaitableFlag dummyInitializedOrDeadFlag;
  WaitableFlag dummyShouldStopFlag;
  std::thread dummyThread(
    dummyThreadLoop,
    std::ref(allDeviceInfos),
    logger,
    gpuIdx,
    std::ref(dummyInitializedOrDeadFlag),
    std::ref(dummyShouldStopFlag)
  );
  dummyInitializedOrDeadFlag.waitUntilTrue();

  OpenCLTuneParams untunedConfig = OpenCLTuneParams();
  OpenCLTuneParams currentConfig = initialConfig;

  if(winograd3x3TileSize == 2) {
    out << "Setting winograd3x3TileSize = 2" << endl;
    untunedConfig.conv3x3.INTILE_XSIZE = 4;
    untunedConfig.conv3x3.INTILE_YSIZE = 4;
    untunedConfig.conv3x3.OUTTILE_XSIZE = 2;
    untunedConfig.conv3x3.OUTTILE_YSIZE = 2;
    currentConfig.conv3x3.INTILE_XSIZE = 4;
    currentConfig.conv3x3.INTILE_YSIZE = 4;
    currentConfig.conv3x3.OUTTILE_XSIZE = 2;
    currentConfig.conv3x3.OUTTILE_YSIZE = 2;
  }
  else if(winograd3x3TileSize == 4) {
    out << "Setting winograd3x3TileSize = 4" << endl;
    untunedConfig.conv3x3.INTILE_XSIZE = 6;
    untunedConfig.conv3x3.INTILE_YSIZE = 6;
    untunedConfig.conv3x3.OUTTILE_XSIZE = 4;
    untunedConfig.conv3x3.OUTTILE_YSIZE = 4;
    currentConfig.conv3x3.INTILE_XSIZE = 6;
    currentConfig.conv3x3.INTILE_YSIZE = 6;
    currentConfig.conv3x3.OUTTILE_XSIZE = 4;
    currentConfig.conv3x3.OUTTILE_YSIZE = 4;
  }

  if(!currentConfig.isValid()) {
    out << "Loaded a config but the config was invalid, starting tuning from basic config" << endl;
    currentConfig = untunedConfig;
  }

  double bestXGemmDirectKernelsPerSecond = 0.0;
  {
    OpenCLTuneParams result;
    tuneXGemmDirect(
      currentConfig,
      untunedConfig,
      context,
      commandQueue,
      deviceIdsToUse,
      batchSize,
      nnXLen,
      nnYLen,
      modelInfo,
      full,
      out,
      verboseErrors,
      verboseTuner,
      result,
      bestXGemmDirectKernelsPerSecond
    );
    currentConfig = result;
  }

  {
    OpenCLTuneParams result;
    bool useFP16Storage = false;
    double bestKernelsPerSecond = 0.0;
    tuneXGemm(
      currentConfig,
      untunedConfig,
      context,
      commandQueue,
      deviceIdsToUse,
      batchSize,
      nnXLen,
      nnYLen,
      modelInfo,
      full,
      out,
      useFP16Storage,
      verboseErrors,
      verboseTuner,
      result,
      bestKernelsPerSecond
    );
    currentConfig = result;

    //Start with having nothing enabled by default
    currentConfig.canUseFP16Storage = false;
    currentConfig.canUseFP16Compute = false;
    currentConfig.canUseFP16TensorCores = false;
    currentConfig.canUseFP16TensorCoresFor1x1 = false;
    currentConfig.shouldUseFP16Storage = false;
    currentConfig.shouldUseFP16Compute = false;
    currentConfig.shouldUseFP16TensorCores = false;
    currentConfig.shouldUseFP16TensorCoresFor1x1 = false;
    //Initialize xGemm16 config to the best non-fp16 config, by default
    currentConfig.xGemm16 = currentConfig.xGemm;

    bool shouldTestFP16 = testFP16Mode != enabled_t::False;
    //Try FP16 if allowed
    if(!shouldTestFP16) {
      out << "Not enabling FP16 for anything since FP16 disabled" << endl;
    }
    else {
      const double bestKernelsPerSecondFP32Only = bestKernelsPerSecond;

      //Since FP16 loses precision, require that it be faster by at least this much to use it
      static constexpr double FP16_REQUIRED_SPEEDUP = 1.2;
      //Tensor cores actually sometimes seem to perform better in practice than the tuning indicates
      static constexpr double FP16_TENSORCORE_REQUIRED_SPEEDUP = 0.9;
      bool foundGoodFP16 = false;

      bool shouldTestFP16TensorCores = testFP16TensorCoresMode == enabled_t::True || (testFP16TensorCoresMode == enabled_t::Auto && !foundGoodFP16);
      if(shouldTestFP16TensorCores) {
        {
          OpenCLTuneParams result16;
          double bestKernelsPerSecond16 = 0.0;
          bool suc = tuneHGemmWmma(
            currentConfig,
            untunedConfig,
            context,
            commandQueue,
            deviceIdsToUse,
            batchSize,
            nnXLen,
            nnYLen,
            modelInfo,
            full,
            out,
            verboseErrors,
            verboseTuner,
            result16,
            bestKernelsPerSecond16
          );
          if(!suc) {
            out << "FP16 tensor core tuning failed, assuming no FP16 tensor core support" << endl;
          }
          else if(bestKernelsPerSecond16 / FP16_TENSORCORE_REQUIRED_SPEEDUP < bestKernelsPerSecond) {
            currentConfig = result16;
            currentConfig.canUseFP16Storage = true;
            currentConfig.canUseFP16TensorCores = true;
            out << "FP16 tensor cores not significantly faster, not enabling" << endl;
          }
          else {
            currentConfig = result16;
            currentConfig.canUseFP16Storage = true;
            currentConfig.canUseFP16TensorCores = true;
            currentConfig.shouldUseFP16Storage = true;
            currentConfig.shouldUseFP16TensorCores = true;
            bestKernelsPerSecond = bestKernelsPerSecond16 / FP16_TENSORCORE_REQUIRED_SPEEDUP;
            foundGoodFP16 = true;
            out << "Enabling FP16 tensor cores due to better performance" << endl;
          }
        }
        {
          // Also try tuning FP16 tensor cores for 1x1 convs
          OpenCLTuneParams result16;
          double bestKernelsPerSecond16 = 0.0;
          bool suc = tuneHGemmWmmaNCHW(
            currentConfig,
            untunedConfig,
            context,
            commandQueue,
            deviceIdsToUse,
            batchSize,
            nnXLen,
            nnYLen,
            modelInfo,
            full,
            out,
            verboseErrors,
            verboseTuner,
            result16,
            bestKernelsPerSecond16
          );
          if(!suc) {
            out << "FP16 tensor core tuning failed for 1x1 convs" << endl;
            currentConfig.canUseFP16TensorCoresFor1x1 = false;
            currentConfig.shouldUseFP16TensorCoresFor1x1 = false;
          }
          // If we're using tensor cores normally, AND they're fast enough, then use them for 1x1 convs.
          else if(currentConfig.shouldUseFP16TensorCores && bestKernelsPerSecond16 / FP16_TENSORCORE_REQUIRED_SPEEDUP >= bestXGemmDirectKernelsPerSecond) {
            out << "FP16 tensor cores enabled for 1x1 convs" << endl;
            currentConfig = result16;
            currentConfig.canUseFP16TensorCoresFor1x1 = true;
            currentConfig.shouldUseFP16TensorCoresFor1x1 = true;
          }
          else {
            out << "FP16 tensor cores not enabled for 1x1 convs" << endl;
            currentConfig = result16;
            currentConfig.canUseFP16TensorCoresFor1x1 = true;
            currentConfig.shouldUseFP16TensorCoresFor1x1 = false;
          }
        }
      }

      bool shouldTestFP16Compute = testFP16ComputeMode == enabled_t::True || (testFP16ComputeMode == enabled_t::Auto && device->info.supportsFP16Compute);
      if(shouldTestFP16Compute) {
        OpenCLTuneParams result16;
        double bestKernelsPerSecond16 = 0.0;
        bool suc = tuneXGemm16(
          currentConfig,
          untunedConfig,
          context,
          commandQueue,
          deviceIdsToUse,
          batchSize,
          nnXLen,
          nnYLen,
          modelInfo,
          full,
          out,
          verboseErrors,
          verboseTuner,
          result16,
          bestKernelsPerSecond16
        );

        if(!suc) {
          out << "FP16 compute tuning failed, assuming no FP16 compute support" << endl;
          currentConfig.xGemm16 = currentConfig.xGemm;
        }
        else if(bestKernelsPerSecond16 / FP16_REQUIRED_SPEEDUP < bestKernelsPerSecondFP32Only) {
          currentConfig = result16;
          currentConfig.canUseFP16Compute = true;
          out << "FP16 compute not significantly faster, not enabling" << endl;
        }
        else if(bestKernelsPerSecond16 / FP16_REQUIRED_SPEEDUP < bestKernelsPerSecond) {
          currentConfig = result16;
          currentConfig.canUseFP16Compute = true;
          currentConfig.shouldUseFP16Compute = true;
          out << "FP16 compute not significantly faster than tensor cores, using it generally but using tensor cores for convs" << endl;
        }
        else {
          currentConfig = result16;
          currentConfig.canUseFP16Compute = true;
          currentConfig.canUseFP16Storage = true;
          currentConfig.shouldUseFP16Storage = true;
          currentConfig.shouldUseFP16Compute = true;
          currentConfig.shouldUseFP16TensorCores = false;
          bestKernelsPerSecond = bestKernelsPerSecond16 / FP16_REQUIRED_SPEEDUP;
          foundGoodFP16 = true;
          out << "Enabling FP16 compute due to better performance" << endl;
        }
      }

      bool shouldTestFP16Storage = testFP16StorageMode == enabled_t::True || (testFP16StorageMode == enabled_t::Auto && !foundGoodFP16);
      if(shouldTestFP16Storage) {
        OpenCLTuneParams result16;
        bool useFP16Storage16 = true;
        double bestKernelsPerSecond16 = 0.0;
        bool suc = tuneXGemm(
          currentConfig,
          untunedConfig,
          context,
          commandQueue,
          deviceIdsToUse,
          batchSize,
          nnXLen,
          nnYLen,
          modelInfo,
          full,
          out,
          useFP16Storage16,
          verboseErrors,
          verboseTuner,
          result16,
          bestKernelsPerSecond16
        );

        if(!suc) {
          out << "FP16 storage tuning failed, assuming no FP16 storage support" << endl;
        }
        else if(bestKernelsPerSecond16 / FP16_REQUIRED_SPEEDUP < bestKernelsPerSecond) {
          currentConfig.canUseFP16Storage = true;
          out << "FP16 storage not significantly faster, not enabling on its own" << endl;
        }
        else {
          currentConfig = result16;
          currentConfig.canUseFP16Storage = true;
          currentConfig.shouldUseFP16Storage = true;
          currentConfig.shouldUseFP16Compute = false;
          currentConfig.shouldUseFP16TensorCores = false;
          bestKernelsPerSecond = bestKernelsPerSecond16 / FP16_REQUIRED_SPEEDUP;
          foundGoodFP16 = true;
          out << "Enabling FP16 storage due to better performance" << endl;
        }
      }
    }
  }

  out << "------------------------------------------------------" << endl;
  string maybeFP16CompileOptions;
  if(currentConfig.shouldUseFP16Storage) {
    out << "Using FP16 storage!" << endl;
    maybeFP16CompileOptions += OpenCLKernels::fp16StorageDefine;
  }
  else {
    out << "Using FP32 storage!" << endl;
  }
  if(currentConfig.shouldUseFP16Compute) {
    out << "Using FP16 compute!" << endl;
    maybeFP16CompileOptions += OpenCLKernels::fp16ComputeDefine;
  }
  else {
    out << "Using FP32 compute!" << endl;
  }
  if(currentConfig.shouldUseFP16TensorCores) {
    out << "Using FP16 tensor cores!" << endl;
  }


  {
    OpenCLTuneParams result;
    tuneTransform(
      currentConfig,
      untunedConfig,
      context,
      commandQueue,
      deviceIdsToUse,
      batchSize,
      nnXLen,
      nnYLen,
      modelInfo,
      full,
      out,
      maybeFP16CompileOptions,
      verboseErrors,
      verboseTuner,
      result
    );
    currentConfig = result;
  }

  {
    OpenCLTuneParams result;
    tuneUntransform(
      currentConfig,
      untunedConfig,
      context,
      commandQueue,
      deviceIdsToUse,
      batchSize,
      nnXLen,
      nnYLen,
      modelInfo,
      full,
      out,
      maybeFP16CompileOptions,
      verboseErrors,
      verboseTuner,
      result
    );
    currentConfig = result;
  }

  {
    OpenCLTuneParams result;
    tuneGPool(
      currentConfig,
      untunedConfig,
      context,
      commandQueue,
      deviceIdsToUse,
      batchSize,
      nnXLen,
      nnYLen,
      modelInfo,
      full,
      out,
      maybeFP16CompileOptions,
      verboseErrors,
      verboseTuner,
      result
    );
    currentConfig = result;

  }

  //Copy 5x5 conv parameters over from 3x3 conv parameters
  //Don't spend the time to separately tune, just assume they're reasonable
  currentConfig.conv5x5.transLocalSize0 = currentConfig.conv3x3.transLocalSize0;
  currentConfig.conv5x5.transLocalSize1 = currentConfig.conv3x3.transLocalSize1;
  currentConfig.conv5x5.untransLocalSize0 = currentConfig.conv3x3.untransLocalSize0;
  currentConfig.conv5x5.untransLocalSize1 = currentConfig.conv3x3.untransLocalSize1;
  currentConfig.conv5x5.untransLocalSize2 = currentConfig.conv3x3.untransLocalSize2;

  dummyShouldStopFlag.setPermanently(true);
  dummyThread.join();

  out << "Done tuning" << endl;
  out << "------------------------------------------------------" << endl;
  tunedConfig = currentConfig;
}

string OpenCLTuner::defaultDirectory(bool makeDir, const string& homeDataDirOverride) {
  string dir = HomeData::getHomeDataDir(true,homeDataDirOverride);
  dir += "/opencltuning";
  if(makeDir)
    MakeDir::make(dir);
  return dir;
}

string OpenCLTuner::defaultFileName(const string& gpuName, int nnXLen, int nnYLen, int trunkNumChannels, int modelVersion) {
  string gpuNameForFile;
  for(int i = 0; i<gpuName.length(); i++) {
    char c = gpuName[i];
    if(contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", c))
      gpuNameForFile += c;
  }
  return Global::strprintf("tune%d_gpu%s_x%d_y%d_c%d_mv%d.txt", TUNER_VERSION, gpuNameForFile.c_str(), nnXLen, nnYLen, trunkNumChannels, modelVersion);
}

string OpenCLTuner::defaultFileName(const string& gpuName, int nnXLen, int nnYLen, OpenCLTuner::ModelInfoForTuning modelInfo) {
  return defaultFileName(gpuName, nnXLen, nnYLen, modelInfo.trunkNumChannels, modelInfo.modelVersion);
}

static OpenCLTuneParams loadFromTunerFile(const string& fileName, Logger* logger) {
  OpenCLTuneParams loadedParams = OpenCLTuneParams::load(fileName);
  if(!loadedParams.isValid())
    throw StringError("Loaded parmameters in " + fileName + " were not valid!");
  if(logger != NULL) {
    string message = "Loaded tuning parameters from: " + fileName;
    logger->write(message);
    if(!logger->isLoggingToStdout() && !logger->isLoggingToStderr())
      cerr << message << endl;
  }
  return loadedParams;
}

OpenCLTuneParams OpenCLTuner::loadOrAutoTune(
  string openCLTunerFile,
  const string& homeDataDirOverride,
  const string& gpuName,
  int gpuIdxForTuning,
  Logger* logger,
  bool openCLReTunePerBoardSize,
  int nnXLen,
  int nnYLen,
  enabled_t testFP16Mode,
  enabled_t testFP16StorageMode,
  enabled_t testFP16ComputeMode,
  enabled_t testFP16TensorCoresMode,
  OpenCLTuner::ModelInfoForTuning modelInfo,
  bool full
) {
  if(openCLTunerFile != "") {
    return loadFromTunerFile(openCLTunerFile,logger);
  }

  string dir = OpenCLTuner::defaultDirectory(true,homeDataDirOverride);
  openCLTunerFile = dir + "/" + OpenCLTuner::defaultFileName(gpuName, nnXLen, nnYLen, modelInfo);

  //Try loading the config for the proper size
  try {
    OpenCLTuneParams loadedParams = loadFromTunerFile(openCLTunerFile,logger);
    return loadedParams;
  }
  catch(const StringError& e) {
    (void)e;
  };

  //If not re-tuning per board size, then check if the tune config for the full size is there
  //And set the nnXLen and nnYLen we'll use for tuning to the full size
  if(!openCLReTunePerBoardSize) {
    nnXLen = NNPos::MAX_BOARD_LEN;
    nnYLen = NNPos::MAX_BOARD_LEN;
    openCLTunerFile = dir + "/" + OpenCLTuner::defaultFileName(gpuName, nnXLen, nnYLen, modelInfo);
    try {
      OpenCLTuneParams loadedParams = loadFromTunerFile(openCLTunerFile,logger);
      return loadedParams;
    }
    catch(const StringError& e) {
      (void)e;
    };
  }

  //No configs found at all, so now autotune
  if(logger != NULL) {
    logger->write("No existing tuning parameters found or parseable or valid at: " + openCLTunerFile);
    logger->write("Performing autotuning");
    logger->write("*** On some systems, this may take several minutes, please be patient ***");
  }
  if(logger == NULL || (!logger->isLoggingToStdout() && !logger->isLoggingToStderr())) {
    cerr << "No existing tuning parameters found or parseable or valid at: " << openCLTunerFile << endl;
    cerr << "Performing autotuning" << endl;
    cerr << "*** On some systems, this may take several minutes, please be patient ***" << endl;
  }

  vector<DeviceInfo> allDeviceInfos = DeviceInfo::getAllDeviceInfosOnSystem(logger);
  if(gpuIdxForTuning < 0 || gpuIdxForTuning >= allDeviceInfos.size())
    throw StringError("Requested gpuIdxForTuning for autotuning was not a valid device: " + Global::intToString(gpuIdxForTuning));
  if(allDeviceInfos[gpuIdxForTuning].name != gpuName)
    throw StringError(
      "Requested gpuIdxForTuning for autotuning expected a device with name " +
      gpuName + " but found a device with name " + allDeviceInfos[gpuIdxForTuning].name
    );

  bool enableProfiling = true;
  DevicesContext devicesContext(allDeviceInfos, {gpuIdxForTuning}, logger, enableProfiling);

  OpenCLTuneParams initialParams;
  int batchSize = OpenCLTuner::DEFAULT_BATCH_SIZE;
  bool verboseErrors = false;
  bool verboseTuner = false;
  OpenCLTuneParams results;
  OpenCLTuner::tune(
    initialParams,
    allDeviceInfos,
    devicesContext,
    gpuIdxForTuning,
    batchSize,
    nnXLen,
    nnYLen,
    testFP16Mode,
    testFP16StorageMode,
    testFP16ComputeMode,
    testFP16TensorCoresMode,
    modelInfo,
    full,
    DEFAULT_WINOGRAD_3X3_TILE_SIZE,
    logger,
    cerr,
    verboseErrors,
    verboseTuner,
    results
  );

  OpenCLTuneParams::save(openCLTunerFile, results);
  if(logger != NULL)
    logger->write("Done tuning, saved results to " + openCLTunerFile);
  if(logger == NULL || (!logger->isLoggingToStdout() && !logger->isLoggingToStderr()))
    cerr << "Done tuning, saved results to " << openCLTunerFile << endl;

  return results;

}

void OpenCLTuner::autoTuneEverything(
  const string& homeDataDirOverride,
  int gpuIdxForTuning,
  Logger* logger,
  enabled_t useFP16Mode,
  bool full
) {
  const enabled_t testFP16Mode = useFP16Mode;
  const enabled_t testFP16StorageMode = useFP16Mode;
  const enabled_t testFP16ComputeMode = enabled_t::Auto;
  const enabled_t testFP16TensorCoresMode = enabled_t::Auto;

  if(logger != NULL) {
    logger->write("Performing autotuning for ALL neural net configurations needed for the run!");
    logger->write("*** If this has not already been done, it may take some time, please be patient ***");
  }
  if(logger == NULL || (!logger->isLoggingToStdout() && !logger->isLoggingToStderr())) {
    cerr << "Performing autotuning for ALL neural net configurations needed for the run!" << endl;
    cerr << "*** If this has not already been done, it may take some time, please be patient ***" << endl;
  }

  vector<DeviceInfo> allDeviceInfos = DeviceInfo::getAllDeviceInfosOnSystem(logger);
  bool enableProfiling = true;
  DevicesContext devicesContext(allDeviceInfos, {gpuIdxForTuning}, logger, enableProfiling);
  //Relookup the gpuIdx to handle the case where it was -1 and the user requested a default
  //DevicesContext will have found the default for us.
  gpuIdxForTuning = devicesContext.findGpuExn(gpuIdxForTuning)->info.gpuIdx;
  if(gpuIdxForTuning < 0 || gpuIdxForTuning >= allDeviceInfos.size())
    throw StringError("Requested gpuIdxForTuning for autotuning was not a valid device: " + Global::intToString(gpuIdxForTuning));

  string gpuName = allDeviceInfos[gpuIdxForTuning].name;

  //Just hardcodedly tune all the models that KataGo's main run uses.
  static_assert(NNModelVersion::latestModelVersionImplemented == 16, "");
  vector<ModelInfoForTuning> modelInfos;
  {
    ModelInfoForTuning modelInfo;
    modelInfo.maxConvChannels1x1 = 96;
    modelInfo.maxConvChannels3x3 = 96;
    modelInfo.trunkNumChannels = 96;
    modelInfo.midNumChannels = 96;
    modelInfo.regularNumChannels = 64;
    modelInfo.gpoolNumChannels = 32;
    modelInfo.modelVersion = 8;
    modelInfos.push_back(modelInfo);
  }
  {
    ModelInfoForTuning modelInfo;
    modelInfo.maxConvChannels1x1 = 128;
    modelInfo.maxConvChannels3x3 = 128;
    modelInfo.trunkNumChannels = 128;
    modelInfo.midNumChannels = 128;
    modelInfo.regularNumChannels = 96;
    modelInfo.gpoolNumChannels = 32;
    modelInfo.modelVersion = 8;
    modelInfos.push_back(modelInfo);
  }
  {
    ModelInfoForTuning modelInfo;
    modelInfo.maxConvChannels1x1 = 192;
    modelInfo.maxConvChannels3x3 = 192;
    modelInfo.trunkNumChannels = 192;
    modelInfo.midNumChannels = 192;
    modelInfo.regularNumChannels = 128;
    modelInfo.gpoolNumChannels = 64;
    modelInfo.modelVersion = 8;
    modelInfos.push_back(modelInfo);
  }
  {
    ModelInfoForTuning modelInfo;
    modelInfo.maxConvChannels1x1 = 256;
    modelInfo.maxConvChannels3x3 = 256;
    modelInfo.trunkNumChannels = 256;
    modelInfo.midNumChannels = 256;
    modelInfo.regularNumChannels = 192;
    modelInfo.gpoolNumChannels = 64;
    modelInfo.modelVersion = 8;
    modelInfos.push_back(modelInfo);
  }
  {
    ModelInfoForTuning modelInfo;
    modelInfo.maxConvChannels1x1 = 256;
    modelInfo.maxConvChannels3x3 = 256;
    modelInfo.trunkNumChannels = 256;
    modelInfo.midNumChannels = 256;
    modelInfo.regularNumChannels = 192;
    modelInfo.gpoolNumChannels = 64;
    modelInfo.modelVersion = 10;
    modelInfos.push_back(modelInfo);
  }
  {
    ModelInfoForTuning modelInfo;
    modelInfo.maxConvChannels1x1 = 320;
    modelInfo.maxConvChannels3x3 = 320;
    modelInfo.trunkNumChannels = 320;
    modelInfo.midNumChannels = 320;
    modelInfo.regularNumChannels = 224;
    modelInfo.gpoolNumChannels = 96;
    modelInfo.modelVersion = 10;
    modelInfos.push_back(modelInfo);
  }
  {
    ModelInfoForTuning modelInfo;
    modelInfo.maxConvChannels1x1 = 384;
    modelInfo.maxConvChannels3x3 = 384;
    modelInfo.trunkNumChannels = 384;
    modelInfo.midNumChannels = 192;
    modelInfo.regularNumChannels = 128;
    modelInfo.gpoolNumChannels = 64;
    modelInfo.modelVersion = 11;
    modelInfos.push_back(modelInfo);
  }
  {
    ModelInfoForTuning modelInfo;
    modelInfo.maxConvChannels1x1 = 384;
    modelInfo.maxConvChannels3x3 = 384;
    modelInfo.trunkNumChannels = 384;
    modelInfo.midNumChannels = 192;
    modelInfo.regularNumChannels = 128;
    modelInfo.gpoolNumChannels = 64;
    modelInfo.modelVersion = 14;
    modelInfos.push_back(modelInfo);
  }
  {
    ModelInfoForTuning modelInfo;
    modelInfo.maxConvChannels1x1 = 512;
    modelInfo.maxConvChannels3x3 = 512;
    modelInfo.trunkNumChannels = 512;
    modelInfo.midNumChannels = 256;
    modelInfo.regularNumChannels = 192;
    modelInfo.gpoolNumChannels = 64;
    modelInfo.modelVersion = 15;
    modelInfos.push_back(modelInfo);
  }
  {
    ModelInfoForTuning modelInfo;
    modelInfo.maxConvChannels1x1 = 512;
    modelInfo.maxConvChannels3x3 = 512;
    modelInfo.trunkNumChannels = 512;
    modelInfo.midNumChannels = 256;
    modelInfo.regularNumChannels = 192;
    modelInfo.gpoolNumChannels = 64;
    modelInfo.modelVersion = 16;
    modelInfos.push_back(modelInfo);
  }

  for(ModelInfoForTuning modelInfo : modelInfos) {
    int nnXLen = NNPos::MAX_BOARD_LEN;
    int nnYLen = NNPos::MAX_BOARD_LEN;
    string dir = OpenCLTuner::defaultDirectory(true,homeDataDirOverride);
    string openCLTunerFile = dir + "/" + OpenCLTuner::defaultFileName(gpuName, nnXLen, nnYLen, modelInfo);
    try {
      OpenCLTuneParams loadedParams = loadFromTunerFile(openCLTunerFile,logger);
      (void)loadedParams;
      continue;
    }
    catch(const StringError& e) {
      (void)e;
    };

    OpenCLTuneParams initialParams;
    int batchSize = OpenCLTuner::DEFAULT_BATCH_SIZE;
    bool verboseErrors = false;
    bool verboseTuner = false;
    OpenCLTuneParams results;
    OpenCLTuner::tune(
      initialParams,
      allDeviceInfos,
      devicesContext,
      gpuIdxForTuning,
      batchSize,
      nnXLen,
      nnYLen,
      testFP16Mode,
      testFP16StorageMode,
      testFP16ComputeMode,
      testFP16TensorCoresMode,
      modelInfo,
      full,
      DEFAULT_WINOGRAD_3X3_TILE_SIZE,
      logger,
      cerr,
      verboseErrors,
      verboseTuner,
      results
    );
    OpenCLTuneParams::save(openCLTunerFile, results);
    if(logger != NULL)
      logger->write("Saved tuning results to " + openCLTunerFile);
    if(logger == NULL || (!logger->isLoggingToStdout() && !logger->isLoggingToStderr()))
      cerr << "Saved tuning results to " << openCLTunerFile << endl;
  }

  if(logger != NULL)
    logger->write("All neural net configs autotuned");
  if(logger == NULL || (!logger->isLoggingToStdout() && !logger->isLoggingToStderr()))
    cerr << "All neural net configs autotuned" << endl;
}


#endif
