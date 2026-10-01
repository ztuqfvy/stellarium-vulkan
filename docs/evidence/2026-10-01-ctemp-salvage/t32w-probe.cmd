@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File C:\temp\wt32-suites.ps1 -ProbeOnly > C:\temp\t32w-probe.log 2>&1
