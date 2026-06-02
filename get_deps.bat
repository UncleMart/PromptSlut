@echo off
cd /d "%~dp0"

echo [1/2] Creating vendor directory...
if not exist vendor mkdir vendor

echo [2/2] Downloading headers...
curl -L -o vendor/json.hpp https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
curl -L -o vendor/httplib.h https://raw.githubusercontent.com/yhi/cpp-httplib/master/httplib.h

echo Done. Headers are in vendor/.
pause
