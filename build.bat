@echo off
setlocal
py -m pip install --upgrade ufbt || exit /b 1
ufbt update --channel release || exit /b 1
ufbt || exit /b 1
echo Build finished. Look for bruce_lite.fap in dist
