@echo off
set T32_PROBE_EXPECT=IT-05,IT-06,IT-13,IT-16,IT-17,IT-18
powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\wt32-suites.ps1 > C:\temp\t32w-full.log 2>&1
