#pragma once
#include "targetver.h"
#include <algorithm>
#include <chrono>
#include <cpr/cpr.h>
#include <cpr/multipart.h>
#include <date.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>
#include <numeric>
#include <regex>
#include <set>
#include <stdio.h>
#include <string> 
#include <tchar.h>
#include <zipper/zipper.h>
#include <zipper/unzipper.h>
#include <zipper/tools.h>
#include <windows.h>
#include <IniReader.h>

#include <RestartManager.h>
#pragma comment(lib ,"Rstrtmgr.lib")

#include <Commctrl.h>
#pragma comment(lib,"Comctl32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
