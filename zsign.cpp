#include "bundle.h"
#include "common/common.h"
#include "macho.h"
#include "openssl.h"
#include <dirent.h>
#include <getopt.h>
#include <libgen.h>
#include <string>
#include <vector>

const struct option options[] = {
    {"debug", no_argument, NULL, 'd'},
    {"force", no_argument, NULL, 'f'},
    {"verbose", no_argument, NULL, 'v'},
    {"cert", required_argument, NULL, 'c'},
    {"pkey", required_argument, NULL, 'k'},
    {"prov", required_argument, NULL, 'm'},
    {"password", required_argument, NULL, 'p'},
    {"bundle_id", required_argument, NULL, 'b'},
    {"bundle_name", required_argument, NULL, 'n'},
    {"bundle_version", required_argument, NULL, 'r'},
    {"entitlements", required_argument, NULL, 'e'},
    {"output", required_argument, NULL, 'o'},
    {"zip_level", required_argument, NULL, 'z'},
    {"dylib", required_argument, NULL, 'l'},
    {"weak", no_argument, NULL, 'w'},
    {"install", no_argument, NULL, 'i'},
    {"info", no_argument, NULL, 'I'},
    {"quiet", no_argument, NULL, 'q'},
    {"no-embed-profile", no_argument, NULL, 'E'},
    {"help", no_argument, NULL, 'h'},
    {}};

int usage() {
  ZLog::Print("Usage: arksigning [-options] [-k privkey.pem] [-m dev.prov] [-o "
              "output.ipa] file|folder\n");
  ZLog::Print("options:\n");
  ZLog::Print(
      "-k, --pkey\t\tPath to private key or p12 file. (PEM or DER format)\n");
  ZLog::Print("-m, --prov\t\tPath to mobile provisioning profile.\n");
  ZLog::Print("-c, --cert\t\tPath to certificate file. (PEM or DER format)\n");
  ZLog::Print(
      "-d, --debug\t\tGenerate debug output files. (.arksigning_debug folder)\n");
  ZLog::Print("-f, --force\t\tForce sign without cache when signing folder.\n");
  ZLog::Print("-o, --output\t\tPath to output ipa file.\n");
  ZLog::Print("-p, --password\t\tPassword for private key or p12 file.\n");
  ZLog::Print("-b, --bundle_id\t\tNew bundle id to change.\n");
  ZLog::Print("-n, --bundle_name\tNew bundle name to change.\n");
  ZLog::Print("--info, --info\t\tOutput app information in JSON format.\n");
  ZLog::Print("-r, --bundle_version\tNew bundle version to change.\n");
  ZLog::Print("-e, --entitlements\tNew entitlements to change.\n");
  ZLog::Print(
      "-z, --zip_level\t\tCompressed level when output the ipa file. (0-9)\n");
  ZLog::Print("-l, --dylib\t\tPath to inject dylib file.\n");
  ZLog::Print(
      "\t\t\tUse -l multiple time to inject multiple dylib files at once.\n");
  ZLog::Print("-w, --weak\t\tInject dylib as LC_LOAD_WEAK_DYLIB.\n");
  ZLog::Print("-i, --install\t\tInstall ipa file using ideviceinstaller "
              "command for test.\n");
  ZLog::Print("-q, --quiet\t\tQuiet operation.\n");
  ZLog::Print("-E, --no-embed-profile\tDon't generate embedded mobile provision.\n");
  ZLog::Print("-v, --version\t\tShows version.\n");
  ZLog::Print("-h, --help\t\tShows help (this message).\n");

  return -1;
}

int main(int argc, char *argv[]) {
  ZTimer gtimer;

  bool bForce = false;
  bool bInstall = false;
  bool bWeakInject = false;
  bool bDontEmbedProfile = false;
  uint32_t uZipLevel = 0;

  string strCertFile;
  string strPKeyFile;
  string strProvFile;
  string strPassword;
  string strBundleId;
  string strBundleVersion;
  string strOutputFile;
  string strDisplayName;
  string strEntitlementsFile;
  string strPath;

  vector<string> arrDyLibFiles;

  int opt = 0;
  int argslot = -1;
  while (-1 != (opt = getopt_long(argc, argv, "dfvhc:k:m:o:ip:e:b:n:z:ql:wE",
                                  options, &argslot))) {
    switch (opt) {
    case 'd':
      ZLog::SetLogLever(ZLog::E_DEBUG);
      break;
    case 'f':
      bForce = true;
      break;
    case 'c':
      strCertFile = optarg;
      break;
    case 'k':
      strPKeyFile = optarg;
      break;
    case 'm':
      strProvFile = optarg;
      break;
    case 'p':
      strPassword = optarg;
      break;
    case 'b':
      strBundleId = optarg;
      break;
    case 'r':
      strBundleVersion = optarg;
      break;
    case 'n':
      strDisplayName = optarg;
      break;
    case 'e':
      strEntitlementsFile = optarg;
      break;
    case 'l':
      arrDyLibFiles.push_back(optarg);
      break;
    case 'i':
      bInstall = true;
      break;
    case 'o':
      strOutputFile = GetCanonicalizePath(optarg);
      break;
    case 'z':
      uZipLevel = atoi(optarg);
      break;
    case 'w':
      bWeakInject = true;
      break;
    case 'E':
      bDontEmbedProfile = true;
      break;
    case 'q':
      ZLog::SetLogLever(ZLog::E_NONE);
      break;
    case 'v':
      printf("version: 0.5\n");
      return 0;
    case 'h':
    case '?':
      return usage();
    case 'I': {
      strPath = GetCanonicalizePath(argv[optind]);
      if (!IsFolder(strPath.c_str()) && !IsZipFile(strPath.c_str())) {
        ZLog::ErrorV(">>> Invalid input file! Please provide an IPA file or app folder.\n");
        return -1;
      }
      
      string strFolder = strPath;
      ZTimer timer;
      if (IsZipFile(strPath.c_str())) {
        StringFormat(strFolder, "/tmp/arksigning_info_%llu", timer.Reset());
        ZLog::PrintV(">>> Unzip:\t%s -> %s ... \n", strPath.c_str(), strFolder.c_str());
        RemoveFolder(strFolder.c_str());
        if (!SystemExec("unzip -qq -d '%s' '%s'", strFolder.c_str(), strPath.c_str())) {
          RemoveFolder(strFolder.c_str());
          ZLog::ErrorV(">>> Unzip Failed!\n");
          return -1;
        }
      }
      
      ZAppBundle bundle;
      bundle.m_strAppFolder = strFolder;
      JValue jvInfo;
      if (bundle.GetAppInfoJson(jvInfo)) {
        string strJson;
        jvInfo.styleWrite(strJson);
        printf("%s\n", strJson.c_str());
      }
      
      if (0 == strFolder.find("/tmp/arksigning_info_")) {
        RemoveFolder(strFolder.c_str());
      }
      return 0;
    }
    }
    ZLog::DebugV(">>> Option:\t-%c, %s\n", opt, optarg);
  }

  if (optind >= argc) {
    return usage();
  }

  strPath = GetCanonicalizePath(argv[optind]);
  if (!IsFileExists(strPath.c_str())) {
    ZLog::ErrorV(">>> Invalid Path! %s\n", strPath.c_str());
    return -1;
  }

  if (ZLog::IsDebug()) {
    CreateFolder("./.arksigning_debug");
    for (int i = optind; i < argc; i++) {
      ZLog::DebugV(">>> Argument:\t%s\n", argv[i]);
    }
  }

  bool bZipFile = false;
  if (!IsFolder(strPath.c_str())) {
    bZipFile = IsZipFile(strPath.c_str());
    if (!bZipFile) { // macho file
      ZMachO macho;
      if (macho.Init(strPath.c_str())) {
        if (!arrDyLibFiles.empty()) { // inject dylib
          bool bCreate = false;
          for (string dyLibFile : arrDyLibFiles)
            macho.InjectDyLib(bWeakInject, dyLibFile.c_str(), bCreate);
        } else {
          macho.PrintInfo();
        }
        macho.Free();
      }
      return 0;
    }
  }

  ZTimer timer;
  arksigningAsset arksigningAsset;
  if (!arksigningAsset.Init(strCertFile, strPKeyFile, strProvFile,
                       strEntitlementsFile, strPassword)) {
    return -1;
  }

  bool bEnableCache = true;
  string strFolder = strPath;
  if (bZipFile) {
    bForce = true;
    bEnableCache = false;
    StringFormat(strFolder, "/tmp/arksigning_folder_%llu", timer.Reset());
    ZLog::PrintV(">>> Unzip:\t%s (%s) -> %s ... \n", strPath.c_str(),
                 GetFileSizeString(strPath.c_str()).c_str(), strFolder.c_str());
    RemoveFolder(strFolder.c_str());
    if (!SystemExec("unzip -qq -d '%s' '%s'", strFolder.c_str(),
                    strPath.c_str())) {
      RemoveFolder(strFolder.c_str());
      ZLog::ErrorV(">>> Unzip Failed!\n");
      return -1;
    }
    timer.PrintResult(true, ">>> Unzip OK!");
  }

  timer.Reset();
  ZAppBundle bundle;
  bool bRet = bundle.SignFolder(&arksigningAsset, strFolder, strBundleId,
                                strBundleVersion, strDisplayName, arrDyLibFiles,
                                bForce, bWeakInject, bEnableCache,
                                bDontEmbedProfile);
  timer.PrintResult(bRet, ">>> Signed %s!", bRet ? "OK" : "Failed");

  if (bInstall && strOutputFile.empty()) {
    StringFormat(strOutputFile, "/tmp/arksigning_temp_%llu.ipa", GetMicroSecond());
  }

  if (!strOutputFile.empty()) {
    timer.Reset();
    size_t pos = bundle.m_strAppFolder.rfind("/Payload");
    if (string::npos == pos) {
      ZLog::Error(">>> Can't Find Payload Directory!\n");
      return -1;
    }

    ZLog::PrintV(">>> Archiving: \t%s ... \n", strOutputFile.c_str());
    string strBaseFolder = bundle.m_strAppFolder.substr(0, pos);
    char szOldFolder[PATH_MAX] = {0};
    if (NULL != getcwd(szOldFolder, PATH_MAX)) {
      if (0 == chdir(strBaseFolder.c_str())) {
        uZipLevel = uZipLevel > 9 ? 9 : uZipLevel;
        RemoveFile(strOutputFile.c_str());
        SystemExec("zip -q -%u -r '%s' Payload", uZipLevel,
                   strOutputFile.c_str());
        chdir(szOldFolder);
        if (!IsFileExists(strOutputFile.c_str())) {
          ZLog::Error(">>> Archive Failed!\n");
          return -1;
        }
      }
    }
    timer.PrintResult(true, ">>> Archive OK! (%s)",
                      GetFileSizeString(strOutputFile.c_str()).c_str());
  }

  if (bRet && bInstall) {
    SystemExec("ideviceinstaller -i '%s'", strOutputFile.c_str());
  }

  if (0 == strOutputFile.find("/tmp/arksigning_tmp_")) {
    RemoveFile(strOutputFile.c_str());
  }

  if (0 == strFolder.find("/tmp/arksigning_folder_")) {
    RemoveFolder(strFolder.c_str());
  }

  gtimer.Print(">>> Done.");
  return bRet ? 0 : -1;
}
